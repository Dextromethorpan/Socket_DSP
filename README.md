Real-Time Windows ↔ WSL Audio Processing via TCP (RtAudio + FFTW)

----------------------------------------

Windows Audio Client Features:
.Built using RtAudio (WASAPI/DSOUND)
.Captures microphone audio
.Streams audio blocks over TCP to WSL
.Receives processed blocks and plays them
.Real-time safe audio callback (no allocations, no locks)
.Uses SPSC ring buffers for thread-safe communication
.CMake-based build system (no vcpkg required)

WSL DSP Server Features:
.Pure TCP DSP, no audio devices
.Uses FFTW (single precision) for spectral processing
.Pre-allocates FFT buffers and plans at startup
.Receives raw float32 samples from Windows
.Produces real-time audio effects (robot voice, lowpass, etc.)
.Sends processed blocks back to Windows

------------------------------------------

WSL Ubuntu-DSP Server Build Instructions:
sudo apt update
sudo apt install -y g++ cmake libfftw3-dev
cd WSL_DSP_Server
cmake..
make -j$(nproc)
./dsp_server

Windows - Audio CLient Build Instructions:
x64 Native Tools Command Prompt for VS:
cd Windows_Audio_Client
mkdir build
cd build
cmake -G "NMake Makefiles" ..
nmake
audio_client.exe


