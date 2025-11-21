cmake_minimum_required(VERSION 3.10)
project(dsp_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(dsp_server dsp_server.cpp)

# Find FFTW3f (single precision)
find_library(FFTW3F_LIB fftw3f REQUIRED)

target_link_libraries(dsp_server PRIVATE ${FFTW3F_LIB} m)
