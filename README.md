🎧 Socket_DSP
Real-Time Windows ↔ WSL Audio Processing via TCP (RtAudio + FFTW)

This project implements a two-program real-time audio DSP system:

Windows Audio Client (RtAudio, WASAPI/DSOUND)
        ⇅ TCP (float32 blocks)
WSL Ubuntu DSP Server (FFTW)


The Windows program handles audio input/output, and the WSL program performs real-time DSP using FFTW (FFT → spectral processing → IFFT) and sends the processed audio back.

🚀 Features
Windows Audio Client

Built using RtAudio (WASAPI/DSOUND)

Captures microphone audio

Streams audio blocks over TCP to WSL

Receives processed blocks and plays them

Real-time safe audio callback (no allocations, no locks)

Uses SPSC ring buffers for thread-safe communication

CMake-based build system (no vcpkg required)

WSL DSP Server

Pure TCP DSP, no audio devices

Uses FFTW (single precision) for spectral processing

Pre-allocates FFT buffers and plans at startup

Receives raw float32 samples from Windows

Produces real-time audio effects (robot voice, lowpass, etc.)

Sends processed blocks back to Windows

🧠 DSP Effect Included (Robot Voice)

The DSP server implements a robot voice effect by:

FFT

Removing phase randomness

Keeping only magnitude

Reapplying a fixed phase grid

IFFT

Normalize

This generates the characteristic metal/robot tone.

📁 Project Structure
Socket_DSP/
│
├── Windows_Audio_Client/
│   ├── audio_client.cpp
│   ├── RtAudio.cpp
│   ├── RtAudio.h
│   ├── CMakeLists.txt
│   └── build/ (generated)
│
└── WSL_DSP_Server/
    ├── dsp_server.cpp
    ├── CMakeLists.txt
    └── build/ (generated)

🔧 Build Instructions
WSL Ubuntu – DSP Server
sudo apt update
sudo apt install -y g++ cmake libfftw3-dev

cd WSL_DSP_Server
mkdir build && cd build
cmake ..
make -j$(nproc)

./dsp_server

Windows – Audio Client
✔ Requirements

Visual Studio 2022 (Desktop C++ workload)

CMake ≥ 3.20

RtAudio.cpp / RtAudio.h included in project

Build

Open x64 Native Tools Command Prompt for VS:

cd Windows_Audio_Client
mkdir build
cd build
cmake -G "NMake Makefiles" ..
nmake


Run:

audio_client.exe

▶️ Run Instructions

Start WSL DSP Server

cd WSL_DSP_Server/build
./dsp_server


Start Windows Audio Client

audio_client.exe


Speak into the microphone — you will hear:

live monitoring

processed robot-voice output

🔌 Communication Protocol

TCP stream, port 4242

Raw float32 little-endian

Block size: 1024 samples (mono)

Direction:

WINDOWS → block of 1024 float samples
WSL → FFT → DSP → IFFT → block of 1024 samples
WINDOWS ← processed audio
