#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

// Only if needed — otherwise REMOVE
#include <windows.h>

#include "RtAudio.h"



#include <iostream>
#include <array>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdint>





constexpr unsigned int SAMPLE_RATE   = 48000;
constexpr unsigned int CHANNELS      = 1;         // mono for simplicity
constexpr unsigned int BLOCK_SIZE    = 1024;      // frames
constexpr size_t       BLOCK_BYTES   = BLOCK_SIZE * sizeof(float);
constexpr const char*  SERVER_HOST   = "127.0.0.1";
constexpr int          SERVER_PORT   = 4242;

// Simple single-producer single-consumer lock-free ring buffer for fixed-size blocks.
template <size_t NumBlocks>
class SpscRingBuffer
{
public:
    SpscRingBuffer()
        : writeIndex_(0), readIndex_(0)
    {}

    // Producer (single thread): push block if space available
    bool push(const float* data)
    {
        size_t currentWrite = writeIndex_.load(std::memory_order_relaxed);
        size_t nextWrite = (currentWrite + 1) % NumBlocks;
        size_t currentRead = readIndex_.load(std::memory_order_acquire);

        if (nextWrite == currentRead) {
            // Buffer full
            return false;
        }

        std::memcpy(buffer_[currentWrite].data(), data, BLOCK_BYTES);
        writeIndex_.store(nextWrite, std::memory_order_release);
        return true;
    }

    // Consumer (single thread): pop block if available
    bool pop(float* out)
    {
        size_t currentRead = readIndex_.load(std::memory_order_relaxed);
        size_t currentWrite = writeIndex_.load(std::memory_order_acquire);

        if (currentRead == currentWrite) {
            // Buffer empty
            return false;
        }

        std::memcpy(out, buffer_[currentRead].data(), BLOCK_BYTES);
        size_t nextRead = (currentRead + 1) % NumBlocks;
        readIndex_.store(nextRead, std::memory_order_release);
        return true;
    }

private:
    std::array<std::array<float, BLOCK_SIZE>, NumBlocks> buffer_;
    std::atomic<size_t> writeIndex_;
    std::atomic<size_t> readIndex_;
};

struct SharedState
{
    std::atomic<bool> running{true};

    // Input from audio callback to network
    SpscRingBuffer<8> inputRing;

    // Output from network to audio callback
    SpscRingBuffer<8> outputRing;
};

// Helper: send all bytes over Winsock
bool sendAll(SOCKET sock, const void* buffer, size_t bytes)
{
    const char* buf = static_cast<const char*>(buffer);
    size_t total = 0;
    while (total < bytes) {
        int n = ::send(sock, buf + total, static_cast<int>(bytes - total), 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

// Helper: receive exactly bytes over Winsock
bool recvAll(SOCKET sock, void* buffer, size_t bytes)
{
    char* buf = static_cast<char*>(buffer);
    size_t total = 0;
    while (total < bytes) {
        int n = ::recv(sock, buf + total, static_cast<int>(bytes - total), 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

// Establish TCP connection to the DSP server (with retry loop)
SOCKET connectToServer(const char* host, int port, std::atomic<bool>& running)
{
    while (running.load()) {
        SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::cerr << "socket() failed: " << WSAGetLastError() << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));

        if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
            std::cerr << "inet_pton() failed.\n";
            ::closesocket(sock);
            return INVALID_SOCKET;
        }

        std::cout << "Connecting to DSP server " << host << ":" << port << "...\n";
        if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            std::cerr << "connect() failed: " << WSAGetLastError()
                      << " (will retry)\n";
            ::closesocket(sock);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        std::cout << "Connected to DSP server.\n";
        return sock;
    }

    return INVALID_SOCKET;
}

// Network thread: moves blocks between rings and the TCP connection
void networkThreadFunc(SharedState* state)
{
    while (state->running.load()) {
        SOCKET sock = connectToServer(SERVER_HOST, SERVER_PORT, state->running);
        if (sock == INVALID_SOCKET) {
            break; // running probably false or unrecoverable error
        }

        float inBlock[BLOCK_SIZE];
        float outBlock[BLOCK_SIZE];

        while (state->running.load()) {
            if (!state->inputRing.pop(inBlock)) {
                // No audio data ready yet, wait a bit
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // Send audio block to DSP server
            if (!sendAll(sock, inBlock, BLOCK_BYTES)) {
                std::cerr << "sendAll() failed, reconnecting...\n";
                break;
            }

            // Receive processed block
            if (!recvAll(sock, outBlock, BLOCK_BYTES)) {
                std::cerr << "recvAll() failed, reconnecting...\n";
                break;
            }

            // Enqueue processed block for callback; if full, drop
            if (!state->outputRing.push(outBlock)) {
                // Optional: if output ring is full, the audio thread is lagging.
                // We drop this processed block to keep latency bounded.
            }
        }

        ::closesocket(sock);
        std::cout << "Disconnected from DSP server. Will retry...\n";
    }

    std::cout << "Network thread exiting.\n";
}

// RtAudio callback
int audioCallback(
    void* outputBuffer,
    void* inputBuffer,
    unsigned int nFrames,
    double /*streamTime*/,
    RtAudioStreamStatus status,
    void* userData)
{
    auto* state = static_cast<SharedState*>(userData);
    float* in  = static_cast<float*>(inputBuffer);
    float* out = static_cast<float*>(outputBuffer);

    if (status) {
        // Buffer over/underflow indication; avoid printing here in real-time code.
        // Just ignore for now.
    }

    if (nFrames != BLOCK_SIZE) {
        // For simplicity, we expect fixed block size. If not, just bypass.
        std::memcpy(out, in, nFrames * sizeof(float));
        return 0;
    }

    // Push input block into ring (non-blocking)
    if (!state->inputRing.push(in)) {
        // If full, we drop this block. Outgoing will use fallback below.
    }

    // Try to get processed block from DSP; if empty, fall back to dry signal
    float processed[BLOCK_SIZE];
    if (state->outputRing.pop(processed)) {
        std::memcpy(out, processed, BLOCK_BYTES);
    } else {
        // No processed block ready yet -> bypass input (dry signal)
        std::memcpy(out, in, BLOCK_BYTES);
    }

    return 0;
}

int main()
{
    // Initialize Winsock
    WSADATA wsaData;
    int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaErr != 0) {
        std::cerr << "WSAStartup failed: " << wsaErr << "\n";
        return 1;
    }

    SharedState state;

    // Start network thread
    std::thread netThread(networkThreadFunc, &state);

    // Setup RtAudio
    RtAudio audio;
    if (audio.getDeviceCount() < 1) {
        std::cerr << "No audio devices found.\n";
        state.running.store(false);
        netThread.join();
        WSACleanup();
        return 1;
    }

    RtAudio::StreamParameters inParams, outParams;
    inParams.deviceId  = audio.getDefaultInputDevice();
    inParams.nChannels = CHANNELS;
    inParams.firstChannel = 0;

    outParams.deviceId  = audio.getDefaultOutputDevice();
    outParams.nChannels = CHANNELS;
    outParams.firstChannel = 0;

    unsigned int bufferFrames = BLOCK_SIZE;

    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_HOG_DEVICE | RTAUDIO_MINIMIZE_LATENCY;

    RtAudioErrorType err;

    err = audio.openStream(
        &outParams,
        &inParams,
        RTAUDIO_FLOAT32,
        SAMPLE_RATE,
        &bufferFrames,
        &audioCallback,
        &state,
        &options
    );

    if (err != RTAUDIO_NO_ERROR) {
        std::cerr << "openStream error: "
              << audio.getErrorText() << "\n";
        state.running.store(false);
        netThread.join();
        WSACleanup();
        return 1;
    }

    err = audio.startStream();
    if (err != RTAUDIO_NO_ERROR) {
        std::cerr << "startStream error: "
              << audio.getErrorText() << "\n";
        state.running.store(false);
        audio.closeStream();
        netThread.join();
        WSACleanup();
        return 1;
    }

    

    if (netThread.joinable()) netThread.join();

    WSACleanup();
    std::cout << "Exiting.\n";
    return 0;
}
