cmake_minimum_required(VERSION 3.20)
project(livebpm VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Strict warnings
if(MSVC)
 add_compile_options(/W4)
else()
 add_compile_options(-Wall -Wextra -Wpedantic)
endif()
# --- KissFFT (C library) ---
add_library(kissfft STATIC extern/kissfft/kiss_fft.c)
target_include_directories(kissfft PUBLIC extern/kissfft)
# --- DSP Library ---
add_library(livebpm_dsp
 dsp/src/SpectralAnalysis.cpp
 dsp/src/OnsetDetection.cpp
 dsp/src/TempoEstimation.cpp
 dsp/src/BeatTrackingDP.cpp
)
target_include_directories(livebpm_dsp PUBLIC
 dsp/include
 core/include
)
target_link_libraries(livebpm_dsp PUBLIC kissfft)

# --- Core Library (Engine) ---
add_library(livebpm_core
 core/src/Engine.cpp
)
target_include_directories(livebpm_core PUBLIC core/include)
target_link_libraries(livebpm_core PUBLIC livebpm_dsp)
# --- Tests ---
include(FetchContent)
FetchContent_Declare(
 Catch2
 GIT_REPOSITORY https://github.com/catchorg/Catch2.git
 GIT_TAG v3.5.0
)
FetchContent_MakeAvailable(Catch2)
add_executable(livebpm_tests
 tests/unit/test_SpectralAnalysis.cpp
 tests/unit/test_BeatTrackingDP.cpp
)
target_link_libraries(livebpm_tests PRIVATE livebpm_core
Catch2::Catch2WithMain)
target_include_directories(livebpm_tests PRIVATE tests/helpers)
include(CTest)
include(Catch)
catch_discover_tests(livebpm_tests)

# --- CLI (will be activated in Week 4) ---
# add_executable(livebpm_cli cli/main.cpp)
# target_link_libraries(livebpm_cli PRIVATE livebpm_core)
add_executable(livebpm_cli cli/main.cpp)
target_link_libraries(livebpm_cli PRIVATE livebpm_core)
target_include_directories(livebpm_cli PRIVATE extern/miniaudio)
# miniaudio needs some links on Linux
if(UNIX AND NOT APPLE)
 target_link_libraries(livebpm_cli PRIVATE pthread dl m)
endif()
