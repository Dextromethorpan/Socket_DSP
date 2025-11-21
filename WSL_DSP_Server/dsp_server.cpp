// dsp_server.cpp — ROBOT VOICE VERSION
// WSL / Linux: TCP DSP server using FFTW (single-precision)
// Robot voice = remove phase → keep only magnitude in frequency domain.

#include <iostream>
#include <vector>
#include <atomic>
#include <csignal>
#include <cstring>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include<cmath>

#include <fftw3.h>

constexpr int BLOCK_SIZE = 1024;
constexpr int PORT = 4242;
constexpr size_t BLOCK_BYTES = BLOCK_SIZE * sizeof(float);

std::atomic<bool> gRunning{true};
void signalHandler(int) { gRunning.store(false); }

// -----------------------------------------------------
// Full TCP helpers
// -----------------------------------------------------
bool recvAll(int sockfd, void* buffer, size_t bytes)
{
    char* buf = (char*)buffer;
    size_t total = 0;
    while (total < bytes)
    {
        ssize_t n = recv(sockfd, buf + total, bytes - total, 0);
        if (n <= 0) return false;
        total += size_t(n);
    }
    return true;
}

bool sendAll(int sockfd, const void* buffer, size_t bytes)
{
    const char* buf = (const char*)buffer;
    size_t total = 0;
    while (total < bytes)
    {
        ssize_t n = send(sockfd, buf + total, bytes - total, 0);
        if (n <= 0) return false;
        total += size_t(n);
    }
    return true;
}

// -----------------------------------------------------
// MAIN
// -----------------------------------------------------
int main()
{
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "ROBOT DSP Server starting on port " << PORT << "...\n";

    // Allocate FFT buffers
    fftwf_complex* timeDomain = fftwf_alloc_complex(BLOCK_SIZE);
    fftwf_complex* freqDomain = fftwf_alloc_complex(BLOCK_SIZE);

    if (!timeDomain || !freqDomain)
    {
        std::cerr << "FFTW allocation failed\n";
        return 1;
    }

    // Create FFT plans (slow, but only done once)
    fftwf_plan planFwd = fftwf_plan_dft_1d(
        BLOCK_SIZE, timeDomain, freqDomain, FFTW_FORWARD, FFTW_MEASURE);

    fftwf_plan planInv = fftwf_plan_dft_1d(
        BLOCK_SIZE, freqDomain, timeDomain, FFTW_BACKWARD, FFTW_MEASURE);

    if (!planFwd || !planInv)
    {
        std::cerr << "FFTW plan creation failed\n";
        return 1;
    }

    // Setup TCP server
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(serverFd, (sockaddr*)&addr, sizeof(addr));
    listen(serverFd, 1);

    std::cout << "ROBOT DSP Server ready. Waiting for client...\n";

    std::vector<float> inBlock(BLOCK_SIZE);
    std::vector<float> outBlock(BLOCK_SIZE);

    // -----------------------------------------------------
    // Accept client
    // -----------------------------------------------------
    while (gRunning)
    {
        sockaddr_in client;
        socklen_t len = sizeof(client);
        int clientFd = accept(serverFd, (sockaddr*)&client, &len);
        if (clientFd < 0) continue;

        std::cout << "Client connected.\n";

        while (gRunning)
        {
            // Receive audio block
            if (!recvAll(clientFd, inBlock.data(), BLOCK_BYTES))
            {
                std::cout << "Client disconnected\n";
                break;
            }

            // -----------------------------
            // 1. Load input into FFT buffer
            // -----------------------------
            for (int i = 0; i < BLOCK_SIZE; i++)
            {
                timeDomain[i][0] = inBlock[i];
                timeDomain[i][1] = 0.0f;
            }

            // -----------------------------
            // 2. Forward FFT
            // -----------------------------
            fftwf_execute(planFwd);

            // -----------------------------
            // 3. ROBOT DSP EFFECT
            // -----------------------------
            for (int k = 0; k < BLOCK_SIZE; k++)
            {
                float real = freqDomain[k][0];
                float imag = freqDomain[k][1];

                // magnitude = sqrt(real^2 + imag^2)
                float mag = sqrtf(real * real + imag * imag);

                // Remove phase → replace with constant phase = 0
                freqDomain[k][0] = mag;   // real part magnitude
                freqDomain[k][1] = 0.0f;  // imag = 0
            }

            // Optional: small low-pass (remove very high frequency buzz)
            for (int k = BLOCK_SIZE / 2; k < BLOCK_SIZE; k++)
            {
                freqDomain[k][0] *= 0.05f;
            }

            // -----------------------------
            // 4. Inverse FFT
            // -----------------------------
            fftwf_execute(planInv);

            // -----------------------------
            // 5. Normalize
            // -----------------------------
            for (int i = 0; i < BLOCK_SIZE; i++)
                outBlock[i] = timeDomain[i][0] / float(BLOCK_SIZE);

            // -----------------------------
            // 6. Send block back
            // -----------------------------
            if (!sendAll(clientFd, outBlock.data(), BLOCK_BYTES))
                break;
        }

        close(clientFd);
    }

    // Cleanup
    fftwf_destroy_plan(planFwd);
    fftwf_destroy_plan(planInv);
    fftwf_free(timeDomain);
    fftwf_free(freqDomain);
    fftwf_cleanup();

    close(serverFd);
    return 0;
}
