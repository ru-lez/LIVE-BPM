# LiveBPM — Android

A native Android port of LiveBPM. It **reuses the exact same C++ DSP engine** as
the web and CLI builds (`../livebpm/core`, `../livebpm/dsp`, `../livebpm/extern/kissfft`)
by compiling those sources into a JNI library — no algorithm is reimplemented.
The web build under `../livebpm/wasm` is untouched and still works independently.

This first version is **microphone-only** and mirrors the web frontend's look and
controls (BPM readout, confidence bar, note-resolution selector, and an Advanced
menu with instrument presets and all tuning sliders).

## Architecture

```
Compose UI (BpmScreen / AdvancedMenu)
        │  state
   BpmViewModel  ── display smoothing (EMA, snap, deadzone) ported from the web
        │
   AudioCapture (AudioRecord, 48 kHz mono float)
        │  float blocks
   NativeBridge (JNI)  ──►  jni_bridge.cpp  ──►  livebpm::Engine  (../livebpm C++)
```

- `app/src/main/cpp/jni_bridge.cpp` is the Android analogue of `../livebpm/wasm/bridge.cpp`.
- To match the web build exactly, raw mic samples are fed straight to the engine
  (no pre-filtering, like the web AudioWorklet), capture uses the least-processed
  mic source (AGC/noise-suppression off), and the display refreshes at the web's
  ~27 ms cadence.
- The engine, web app, CLI and tests in `../livebpm` are not modified.

## Prerequisites

- **Android Studio** (recent — the project targets `compileSdk 36`).
- **Android SDK Platform 36** (already installed on this machine).
- **NDK** and **CMake** — *not yet installed.* Install them once:
  Android Studio → **Settings → Languages & Frameworks → Android SDK → SDK Tools**,
  tick **NDK (Side by side)** and **CMake**, then **Apply**.
  (Android Studio may also offer to install them automatically on first Gradle sync.)

## Build & run

1. Open the `android/` folder in Android Studio (open this folder directly, not
   the repo root).
2. Let Gradle sync. On first sync Android Studio downloads the Gradle wrapper JAR
   and the AGP/Gradle distribution, and will prompt to install the NDK + CMake if
   missing (see above).
3. Pick a device or emulator and press **Run**.
4. Tap **Mic**, grant the microphone permission, and play/clap a steady rhythm.

### Command line (optional)

Once the NDK + CMake are installed:

```
cd android
./gradlew assembleDebug      # or gradlew.bat on Windows cmd
```

The `externalNativeBuild` step cross-compiles `liblivebpm.so` for
`arm64-v8a`, `armeabi-v7a`, and `x86_64`.

## Troubleshooting

- **`ld.lld: error: undefined symbol: operator new / std::...` at link time.**
  Already handled. Because this SDK/NDK lives under a path with spaces
  (`C:\Users\Joe Biden\...`), CMake invokes clang via 8.3 short paths and the NDK's
  `-no-canonical-prefixes` then prevents clang from auto-linking libc++. The build
  links `c++_shared` explicitly (`app/src/main/cpp/CMakeLists.txt`) and sets
  `-DANDROID_STL=c++_shared` (`app/build.gradle.kts`) so AGP packages
  `libc++_shared.so` into the APK. Verified building for `arm64-v8a` and `x86_64`.

## Notes / not yet ported

- WAV file loading, seek and pause (the web "Load WAV" mode) are out of scope for
  this version — it is microphone-only.
- The plugin/Gradle versions in `gradle/libs.versions.toml` are pinned for
  `compileSdk 36`. If your installed Android Gradle Plugin differs, use Android
  Studio's AGP Upgrade Assistant to align them.
