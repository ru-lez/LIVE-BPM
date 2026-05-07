# LIVEBPM by MEHEARSAL

A real-time BPM (Beats Per Minute) detector written in C++. It runs as both a native console application capturing microphone input and a browser-based tool via WebAssembly (WASM).

## Requirements
* C++17 compiler
* CMake 3.20 or higher
* Emscripten SDK
---

## Native Build & CLI

### 1. Building the Native App
From the project root, configure and build using CMake
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### 2. Run CLI
The native command-line application uses `miniaudio` to capture live microphone input and measure BPM in real-time.
```bash
./build/livebpm_cli
```
#### Options:
* `-r <samplerate>` - Set the sample rate (default: 48000)
* `-b <blocksize>` - Set the block size (default: 512)
---


## WebAssembly (WASM) Compilation

### 1. Install Emscripten
Clone the SDK onto a parent folder and activate Empscripten SDK
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
```

Load the Emscripten environment variables
```bash
source ./emsdk_env.sh # Linux/macOS
emsdk_env.bat # Windows
```

Verify that Emscripten works
```bash
emcc --version
```

### 2. Compile to WASM
From the `livebpm` project root, run
```bash
mkdir build-wasm && cd build-wasm
emcmake cmake ..
cmake --build .
```

This generates `livebpm_wasm.js` (JavaScript Loader) & `livebpm_wasm.mwasm` (Binary WebAssembly Module)

### 3. Serve Locally
Ensure you are on the `build-wasm` folder.

Copy the web files
```bash
cp ../wasm/processor.js .
cp ../wasm/index.html .
```

and then start a local server
```bash
python -m http.server 8080 # Python
npx serve . # or Node
```
---


## Running Tests
This project uses Catch2 for unit and integration testing. To run these tests
```bash
cd build
ctest --output-on-failure
```


## Architecture
```text
Audio
  |
  v
[Input Filters]
  |
  v
[RingBuffer]
  |
  v
[FFT + Hann Window]
  |
  +-- Magnitude ---------------------------------+
  |                                              |
  v                                              v
[Semitone Filter Bank Flux]           [Complex Domain Distance]
  |                                              |
  +--------> [Adaptive Gating & Fusion] <--------+
                                      |
                                      v
                             [Novelty Buffer] (4 seconds)
                                      |
            +-------------------------+-------------------------+
            |                                                   |
            v                                                   v
   [FFT Autocorrelation]                               [Beat Tracking DP]
     (Tempo Estimation)                                (Beat Alignment)
            |                                                   |
            +-------------------------+-------------------------+
                                      |
                                      v
                           [Confidence Scoring]
                           (Stability, Coverage)
```


## Dependencies
* **KissFFT**: FFT processing (BSD licence)
* **miniaudio**: Audio I/O (Public domain)
* **Catch2**: Testing framework (BSL-1.0)


## Known Limitations
* TODO


## Authors
* Manmohan Gahir
* Vincent Wang