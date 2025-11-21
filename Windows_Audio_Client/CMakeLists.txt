cmake_minimum_required(VERSION 3.10)
project(audio_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-D__WINDOWS_WASAPI__)

add_executable(audio_client
    audio_client.cpp
    RtAudio.cpp
)

target_link_libraries(audio_client PRIVATE
    ws2_32
    winmm
)
