cmake_minimum_required(VERSION 3.22.1)
project(livebpm_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Reuse the existing, unmodified C++ engine that lives in the sibling `livebpm/`
# folder. Path is relative to this CMakeLists (app/src/main/cpp -> repo root).
set(LIVEBPM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../livebpm)

add_library(livebpm SHARED
    # --- KissFFT (C) ---
    ${LIVEBPM_ROOT}/extern/kissfft/kiss_fft.c
    # --- DSP ---
    ${LIVEBPM_ROOT}/dsp/src/SpectralAnalysis.cpp
    ${LIVEBPM_ROOT}/dsp/src/OnsetDetection.cpp
    ${LIVEBPM_ROOT}/dsp/src/TempoEstimation.cpp
    ${LIVEBPM_ROOT}/dsp/src/BeatTrackingDP.cpp
    # --- Core engine ---
    ${LIVEBPM_ROOT}/core/src/Engine.cpp
    # --- Android JNI bridge ---
    jni_bridge.cpp
)

target_include_directories(livebpm PRIVATE
    ${LIVEBPM_ROOT}/core/include
    ${LIVEBPM_ROOT}/dsp/include
    ${LIVEBPM_ROOT}/extern/kissfft
)

# Force the link step to use the C++ driver (this target mixes C and C++ sources).
set_target_properties(livebpm PROPERTIES LINKER_LANGUAGE CXX)

# Android system log (optional; available if the bridge ever logs).
find_library(log-lib log)

# Link the C++ shared runtime EXPLICITLY (c++_shared). When the SDK/NDK lives under
# a path containing spaces (e.g. "C:\Users\Joe Biden\..."), CMake invokes clang via
# 8.3 short paths, which together with the NDK's -no-canonical-prefixes flag stops
# clang from locating libc++ to auto-link it — leaving every std:: symbol undefined.
# Naming the library here puts -lc++_shared directly on the linker line (resolved via
# the sysroot -L paths), so it no longer depends on clang's self-location.
# Pair this with -DANDROID_STL=c++_shared (set in app/build.gradle.kts) so AGP also
# packages libc++_shared.so into the APK.
target_link_libraries(livebpm c++_shared ${log-lib})

# Align LOAD segments to 16 KB so the library loads on Android 15+ devices that use
# 16 KB memory pages (NDK r27 still defaults to 4 KB; r28+ does this by default).
target_link_options(livebpm PRIVATE "-Wl,-z,max-page-size=16384")
