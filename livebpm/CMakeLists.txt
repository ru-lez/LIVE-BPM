cmake_minimum_required(VERSION 3.20)
project(livebpm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Strict warnings
if(MSVC)
    set(LIVEBPM_WARNINGS /W4)
else()
    set(LIVEBPM_WARNINGS -Wall -Wextra -Wpedantic)
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

target_compile_options(livebpm_dsp PRIVATE
    ${LIVEBPM_WARNINGS}
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

target_compile_options(livebpm_core PRIVATE
    ${LIVEBPM_WARNINGS}
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
    tests/unit/test_BeatTrackingDP.cpp
    tests/unit/test_FullPipeline.cpp
    tests/unit/test_OnsetDetection.cpp
    tests/unit/test_RingBuffer.cpp
    tests/unit/test_SpectralAnalysis.cpp
    tests/unit/test_TempoEstimation.cpp
)

target_compile_options(livebpm_tests PRIVATE
    ${LIVEBPM_WARNINGS}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR EMSCRIPTEN)
    target_compile_options(livebpm_tests PRIVATE -Wno-c2y-extensions)
endif()

target_link_libraries(livebpm_tests PRIVATE livebpm_core
    Catch2::Catch2WithMain
)

target_include_directories(livebpm_tests PRIVATE tests/helpers)

include(CTest)
include(Catch)
catch_discover_tests(livebpm_tests)

# --- CLI ---
add_executable(livebpm_cli cli/main.cpp)
target_compile_options(livebpm_cli PRIVATE ${LIVEBPM_WARNINGS})
target_link_libraries(livebpm_cli PRIVATE livebpm_core)
target_include_directories(livebpm_cli PRIVATE extern/miniaudio)

# miniaudio needs some links on Linux
if(UNIX AND NOT APPLE)
 target_link_libraries(livebpm_cli PRIVATE pthread dl m)
endif()

# --- Accuracy harness (offline algorithm testing: synthetic + WAV manifest) ---
add_executable(livebpm_accuracy tests/accuracy/AccuracyRunner.cpp)
target_compile_options(livebpm_accuracy PRIVATE ${LIVEBPM_WARNINGS})
target_link_libraries(livebpm_accuracy PRIVATE livebpm_core)
target_include_directories(livebpm_accuracy PRIVATE extern/miniaudio tests/helpers)

if(UNIX AND NOT APPLE)
 target_link_libraries(livebpm_accuracy PRIVATE pthread dl m)
endif()

# WASM build (only if compiling with Emscripten)
if (EMSCRIPTEN)
    add_executable(livebpm_wasm wasm/bridge.cpp)
    target_link_libraries(livebpm_wasm PRIVATE livebpm_core)
    target_compile_options(livebpm_wasm PRIVATE ${LIVEBPM_WARNINGS})

    # Enable WebAssembly shared memory instructions for all targets
    target_compile_options(livebpm_wasm PRIVATE -matomics -mbulk-memory)
    target_compile_options(livebpm_core PRIVATE -matomics -mbulk-memory)
    target_compile_options(livebpm_dsp PRIVATE -matomics -mbulk-memory)
    target_compile_options(kissfft PRIVATE -matomics -mbulk-memory)

    # Emscripten flags
    # ENVIRONMENT flag modified to remove single quotes and include 'worklet' due to syntax quirk with CMake and Emscripten
    # Backslashes are removed due to issues with CMake's handling of multi-line strings and to ensure the flags are passed correctly to Emscripten
    # EXPORTED_RUNTIME_METHODS is set to allow direct access to HEAPF32 for efficient data transfer between JS and WASM
    # EXPORTED_FUNCTIONS is set to ensure malloc and free are available for memory management from JS
    set_target_properties(livebpm_wasm PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS " --bind -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=web,worker,worklet -s EXPORT_ES6=1 -s MODULARIZE=1 -s EXPORT_NAME=createLiveBpmModule -s EXPORTED_FUNCTIONS=_malloc,_free -s EXPORTED_RUNTIME_METHODS=HEAPF32 -O2 "

    
    #[[
        set_target_properties(livebpm_wasm PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS " \
        --bind \
        -s WASM=1 \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s ENVIRONMENT=web,worker,worklet \ 
        -s EXPORT_ES6=1 \
        -s MODULARIZE=1 \
        -s EXPORT_NAME='createLiveBpmModule' \
        -s EXPORTED_FUNCTIONS=_malloc,_free \
        -s EXPORTED_RUNTIME_METHODS=HEAPF32
        -O2 \
        "
    ]]
    )
endif()