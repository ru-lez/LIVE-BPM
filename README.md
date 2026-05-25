# LIVEBPM by MUSE SCENE LAB

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
mkdir build-wasm
cd build-wasm
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


## Advanced Parameters

The browser UI exposes an **Advanced** panel with fine-grained controls over the detection engine. Parameters are grouped into three sections.

---

### Onset Detection & Gating

**Noise Gate (Silence)** · default `0.030` · range `0.0 – 0.1`
Sets the absolute volume floor below which all audio is ignored.
- **Low:** Picks up very quiet sounds but risks triggering on background hum or room noise.
- **High:** Silences ambient noise; may miss soft notes or light touches.

**Complex Domain Weight** · default `0.40` · range `0.0 – 1.0`
Balances two onset detection methods: spectral flux (percussive) vs. complex domain distance (tonal).
- **Low:** Favours punchy, percussive attacks (drums, cajón, plucked strings).
- **High:** Tracks smooth, legato instruments (flute, bowed strings) more accurately.

**Onset Sensitivity** · default `0.015` · range `0.001 – 0.1`
The minimum energy jump required to register a beat onset.
- **Low:** Catches quiet or subtle notes; may increase false positives in noisy environments.
- **High:** Only reacts to strong, clear attacks; may miss soft or lightly played notes.

**Gating Alpha (Adaptation)** · default `0.10` · range `0.01 – 0.5`
How quickly the noise gate adapts when the musician's overall volume changes.
- **Low:** Gate adjusts slowly — stable against sudden spikes but lags behind gradual crescendos.
- **High:** Gate adapts immediately to volume changes — more responsive but potentially jittery.

**Flux Compression (Log1p)** · default `50` · range `10 – 100`
Compresses the dynamic range of the audio energy signal before it reaches the tracker.
- **Low:** Preserves dynamic contrast; loud accents carry more weight than soft notes.
- **High:** Flattens differences so quiet notes and loud accents contribute equally to the tempo estimate.

**Novelty Smoothing** · default `0.40` · range `0.05 – 0.9`
Moving average applied to the raw beat-energy stream.
- **Low:** Faster reaction to beat changes; may jitter under unsteady or rubato playing.
- **High:** Smoother, more stable output with a slight delay when tempo shifts suddenly.

---

### Tempo Engine & Hysteresis

**Target Minimum BPM** · default `40` · range `30 – 120`
Lower bound of the tempo search window.
- **Low:** Allows detection of very slow tempos.
- **High:** Prevents the engine from ever locking onto a half-time error below this value.

**Target Maximum BPM** · default `240` · range `120 – 480`
Upper bound of the tempo search window.
- **Low:** Caps detection to moderate tempos; useful to avoid false double-time readings.
- **High:** Allows detection of very fast tempos (fast electronic music, drum rolls).

**Heuristics (Octave/Pattern correction)** · default `On`
Uses rhythmic pattern analysis to guard against double-time and half-time tracking errors.
- **Off:** Raw tempo estimate is used with no octave correction; may report 2× or ½× the true BPM.
- **On:** Automatically corrects common octave errors using pattern heuristics.

**Hysteresis Blend (Stickiness)** · default `0.05` · range `0.01 – 0.2`
Blends the previous tempo estimate into each new reading to dampen sudden jumps.
- **Low:** Display resists all but the most confident changes — very stable but slow to follow a real tempo shift.
- **High:** Adapts quickly to genuine tempo changes; more susceptible to momentary outliers shifting the reading.

**Double-Time Jump Tolerance** · default `20` · range `5 – 40`
Confidence delta required before the engine accepts a sudden doubling of BPM (e.g. 60 → 120).
- **Low:** BPM can jump to double speed with little evidence (less resistant to double-time errors).
- **High:** Engine demands a very strong signal before accepting a 2× jump (more resistant to double-time errors).

**Half-Time Jump Tolerance** · default `10` · range `5 – 30`
Confidence delta required before the engine accepts a sudden halving of BPM (e.g. 120 → 60).
- **Low:** BPM can drop to half speed with little evidence (less resistant to half-time errors).
- **High:** Engine demands a very strong signal before accepting a ½× drop (more resistant to half-time errors).

---

### Display Output

**UI Smoothing (Alpha)** · default `0.15` · range `0.01 – 0.5`
Exponential moving average applied to the displayed BPM number.
- **Low:** Very fluid visual transitions; display lags noticeably behind sudden tempo changes.
- **High:** Display reacts quickly to each engine output; may look jittery during stable playback.

**Visual Deadzone (Drift filter)** · default `0.1` · range `0.0 – 5.0`
Minimum BPM change required before the on-screen number updates.
- **Low (0):** Every micro-fluctuation from the engine is reflected immediately on screen.
- **High:** Display stays locked until the BPM shifts by at least this amount, preventing visual noise.

**Magnetic Snap Range** · default `0.15` · range `0.0 – 0.5`
Snaps the displayed BPM to the nearest whole number when within this range.
- **Low (0):** Shows the raw decimal BPM (e.g. 127.85).
- **High:** Snaps aggressively over a wider range (e.g. 127.4 rounds to 127), keeping the display clean for live musicians.

---

## Known Limitations
* BPM of Legato instruments may take a few seconds to intially recognise (up to 4 seconds).
* Complex rhythms (like 16th-note funk patterns) may still briefly trick the engine until hysteresis lock corrects it.
* Note value switching flushes the DSP to prevent buffer maths errors. This means the engine will be temporarily unavailable (~0.25s or 50 hops) while it rebuilds the novelty curve.


## Authors
* Manmohan Gahir
* Vincent Wang