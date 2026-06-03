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

## Dependencies
* **KissFFT**: FFT processing (BSD licence)
* **miniaudio**: Audio I/O (Public domain)
* **Catch2**: Testing framework (BSL-1.0)

## Architecture
```text
Audio
  |
  v
[RingBuffer]
  |
  v
[FFT + Hann Window] -> Magnitude + Phase
  |
  +---------------------+----------------------+
  |                                            |
  v                                            v
[Semitone Filter Bank Flux]          [Complex Domain Flux]
  |                                            |
  +----------------> [Adaptive Gating] <-------+
                (per-stream moving averages)
                               |
                               v
                      [Fusion + Log Compression]
                               |
                               v
                    [Novelty Buffer] (rolling ~4s)
                               |
            +------------------+------------------+
            |                                     |
            v                                     v
   [Tempo Estimation]                   [Beat Tracking DP]
  (FFT Autocorrelation +              (Dynamic Programming for
  biased peak picking)               beat positions/alignment)
            |                                     |
            +------------------+------------------+
                               |
                               v
                    [Stability & Post-Processing]
                    - Dynamic Tracking Window (+ lock timer)
                    - Alternation Heuristics (gated when locked)
                    - Asymmetric Hysteresis Veto
                    - Final BPM Commit
                               |
                               v
                        Final BPM + Confidence
```

## Engine Overview

```text
/**
 * Engine Overview: Real-Time BPM & Beat Tracking
 * 
 * This engine estimates tempo and beat alignment for live audio streams.
 * It chops the incoming audio into overlapping FFT windows to build a novelty curve.
 * Then, it feeds that data into a rolling history buffer by using periodicity and dynamic
 * programming to lock onto the tempo.
 *
 * 
 * How it Works
 *
 * - Getting the Novelty Curve (Onset Detection)
 *   First, the FFT magnitudes and phases for each audio block are calculated. 
 *   To find the notes, a dual-stream approach is used:
 *     1. Semitone/Super Flux: Tracks energy shifts across log-spaced semitone bands.
 *        It handles hard transients, like percussion.
 *     2. Complex Domain Flux: Tracks both magnitude and phase. It's used to catch the
 *        softer legato transitions (like vocals or bowed strings) that pure magnitude
 *        methods usually miss.
 *   Both streams are run through separate moving averages to create an adaptive gating threshold.
 *   Then, they are fused together and a logarithmic compression is applied to get the
 *   final gated novelty curve.
 *
 * - Getting the Right Tempo
 *   The periodic structure of the novelty buffer is analysed to find the main pulse.
 *   The search window is dynamically scaled depending on the target note resolution.
 *   (e.g. 16th notes vs half notes)
 *
 *   Once a stable tempo is locked, a Dynamic Tracking Window constrains the estimator's
 *   search range to ±X% around the current stable BPM. This dramatically reduces
 *   accidental half-time and double-time jumps. A lock timer combined with a
 *   low-confidence streak counter makes the window tolerant of short sparse or
 *   legato passages without shattering the lock.
 *
 * - Stabilising Readings (Post-Processing & Hysteresis)
 *   Instead of relying purely on the raw tempo estimate, the engine implements heuristics
 *   and hysteresis to prevent erratic jumps, especially in challenging scenarios like
 *   fast tempos or weak signals.
 *     1. Heuristics: The engine looks for strumming or tapping patterns by analysing
 *        amplitude peaks. It compares even/odd peak ratios to make educated guesses to
 *        prevent accidental double-time BPM readings. Heuristics are automatically
 *        disabled once the dynamic window is locked.
 *     2. Hysteresis Lock: The system resists sudden BPM changes based on its history.
 *        An asymmetric veto now strictly blocks half-time candidates while still
 *        allowing legitimate double-time readings, which isimportant for dense percussion.
 *        If the audio signal is muddy or ambiguous, the engine holds onto the last
 *        stable BPM instead of jumping to half-time or double-time.
 * 
 * - UI Smoothing (WASM & CLI Integration)
 *   Due to the core engine designed to output raw precision, the CLI and WASM front-ends
 *   implement smoothing layers to prevent jittery displays and to enhance readability.
 *     1. Magnetic Snapping: BPM readings that are within close thresholds of close numbers
 *        are snapped to those numbers. (e.g. 127.9 BPM would snap to 128 BPM)
 *     2. Confidence Gating: The BPM display is only updated when the engine has a high
 *        confidence reading, preventing erratic jumps when the signal is weak or ambiguous.
 *   Ensuring these smoothing layers are implemented at the front-end layer allows the 
 *   core engine to remain 100% analytical and precise, while the user interfaces 
 *   (CLI and WASM) can focus on readability and usability.
 * 
 */
 ```


## Target Note Resolution

By default, the LiveBPM engine assumes the most prominent rhythmic pulse it hears is the standard "Quarter Note" (the main beat). However, if a musician is playing rapid 16th-note arpeggios or slow, sweeping half-note chords, the engine might report the tempo as 4x too fast or 2x too slow. 

The **Note Resolution** setting allows you to tell the DSP engine exactly how to interpret the rhythmic density of the audio stream.

**1/4 Note (Quarter)** · *Default*
The standard metronome setting. The engine assumes every major pulse it detects equals one beat.
- **Best for:** Standard playing, 4/4 time signatures, and most modern pop/rock/electronic music where the downbeat is clearly defined.

**Smart (Mixed) Mode**
An advanced fusion mode designed to track highly syncopated rhythms without losing the primary master tempo. Instead of locking into a single grid, this mode uses Subdivision Folding. 
- **Best for:** Complex grooves with scattered ghost notes, or drummers who frequently switch between standard rock beats and dense 16th-note hi-hat patterns. 
- **How it works:** The engine evaluates the main Quarter Note pulse, but "folds" the energy of the underlying 8th and 16th notes into the main score. Furthermore, if the tempo exceeds 135 BPM, it applies an aggressive penalty if it detects a strong half-time groove underneath, actively suppressing false double-time errors. Breakthrough logic is disabled to ensure maximum stability.

**1/2 Note (Half)** · *Multiplies engine BPM by 2.0*
The engine assumes the pulses it hears are lasting for two full beats. 
- **Best for:** Ambient music, slow orchestral swells, or doom metal where the dominant rhythmic changes only occur on beats 1 and 3 (or once per bar). It prevents the engine from dropping into an artificially slow BPM.

**1/8 Note (Eighth)** · *Multiplies engine BPM by 0.5*
The engine assumes it is hearing two pulses for every single beat.
- **Best for:** Aggressive 8th-note guitar strumming, driving punk rock basslines, or rapid hi-hat patterns. It prevents the engine from accidentally doubling the reported tempo when the musician is "playing eighths."

**1/8 Note Triplet** · *Multiplies engine BPM by ~0.667*
The engine assumes it is hearing three evenly spaced pulses per beat.
- **Best for:** Blues, shuffle grooves, jazz ride cymbal patterns, and 6/8 or 12/8 time signatures. It ensures the mathematical "swing" doesn't confuse the beat tracker into outputting fractional or unstable tempos.

**1/16 Note (Sixteenth)** · *Multiplies engine BPM by 0.25*
The engine assumes it is hearing four rapid pulses for every single beat.
- **Best for:** Fast electronic/trap hi-hats, rapid acoustic fingerpicking, or complex drum solos. It allows the engine to track incredibly fast subdivisions (e.g., 512 hits per minute) while still displaying the readable master tempo (e.g., 128 BPM).


## Advanced Parameters

The browser UI exposes an **Advanced** panel with fine-grained controls over the detection engine. Parameters are grouped into four logical sections that follow the signal flow of the engine.

---

### Onset Detection

**Onset Sensitivity** · default `0.015` · range `0.005 – 0.1`
The minimum energy jump required to register a beat onset.
- **Low:** Catches quiet or subtle notes; may increase false positives in noisy environments.
- **High:** Only reacts to strong, clear attacks; may miss soft or lightly played notes.

**Complex Domain Weight** · default `0.40` · range `0.0 – 1.0`
Balances two onset detection methods: spectral flux (percussive) vs. complex domain distance (tonal).
- **Low:** Favours punchy, percussive attacks (drums, cajón, plucked strings).
- **High:** Tracks smooth, legato instruments (flute, bowed strings) more accurately.

**Flux Compression (Log1p)** · default `50` · range `10 – 100`
Compresses the dynamic range of the audio energy signal before it reaches the tracker.
- **Low:** Preserves dynamic contrast; loud accents carry more weight than soft notes.
- **High:** Flattens differences so quiet notes and loud accents contribute equally to the tempo estimate.

**Novelty Smoothing** · default `0.40` · range `0.05 – 0.9`
Moving average applied to the raw beat-energy stream.
- **Low:** Faster reaction to beat changes; may jitter under unsteady or rubato playing.
- **High:** Smoother, more stable output with a slight delay when tempo shifts suddenly.

---

### Noise & Adaptive Gating

**Noise Gate (Silence)** · default `0.030` · range `0.0 – 0.1`
Sets the absolute volume floor below which all audio is ignored.
- **Low:** Picks up very quiet sounds but risks triggering on background hum or room noise.
- **High:** Silences ambient noise; may miss soft notes or light touches.

**Gating Alpha (Adaptation)** · default `0.10` · range `0.01 – 0.5`
How quickly the noise gate adapts when the musician's overall volume changes.
- **Low:** Gate adjusts slowly — stable against sudden spikes but lags behind gradual crescendos.
- **High:** Gate adapts immediately to volume changes — more responsive but potentially jittery.

**Silence Timeout** · default `2.0` · range `0.5 – 5.0`
Duration of continuous silence (in seconds) required before the engine completely flushes its rolling history buffer and resets.
- **Low:** Flushes the engine almost immediately during brief pauses, requiring it to start from scratch when playing resumes.
- **High:** Allows the musician to hold long, dramatic pauses without the engine losing the established tempo context.

---

### Tempo Tracking & Locking

**Heuristics (Octave/Pattern correction)** · default `On`
Uses rhythmic pattern analysis to guard against double-time and half-time tracking errors.
- **Off:** Raw tempo estimate is used with no octave correction; may report 2× or ½× the true BPM.
- **On:** Automatically corrects common octave errors using pattern heuristics.

**Dynamic Window Size** · default `0.40` · range `0.10 – 1.00`
Once a stable tempo is locked, restricts the estimator’s search range to ±X% around the current BPM.
- **Low:** Extremely narrow window — highly resistant to half/double-time errors.
- **High:** Wider window — more flexible but allows more risk of octave jumps.

**Shatter Threshold** · default `0.70` · range `0.50 – 0.90`
Confidence level below which the dynamic window is temporarily opened again.
- **Low:** Window stays locked even during uncertain passages (maximum stability).
- **High:** Window shatters more easily on brief dips in confidence (more responsive).

**Target Minimum BPM** · default `40` · range `30 – 120`
Lower bound of the tempo search window.
- **Low:** Allows detection of very slow tempos.
- **High:** Prevents the engine from ever locking onto a half-time error below this value.

**Target Maximum BPM** · default `240` · range `120 – 480`
Upper bound of the tempo search window.
- **Low:** Caps detection to moderate tempos; useful to avoid false double-time readings.
- **High:** Allows detection of very fast tempos (fast electronic music, drum rolls).

**Hysteresis Blend (Stickiness)** · default `0.05` · range `0.01 – 0.2`
Blends the previous tempo estimate into each new reading to dampen sudden jumps.
- **Low:** Display resists all but the most confident changes — very stable but slow to follow a real tempo shift.
- **High:** Adapts quickly to genuine tempo changes; more susceptible to momentary outliers shifting the reading.

**Octave Lock Tolerance** · default `100` · range `10 – 300`
Determines how long the engine will ignore sudden half-time or double-time drops before deciding the tempo change is intentional.
- **Low:** The engine is highly reactive. It will trust sudden octave shifts almost immediately.
- **High:** The engine is highly rigid. It protects the current groove through long, sparse sections (like an acoustic pre-chorus or ambient bridge).

**Fast Tempo Threshold** · default `140.0` · range `100.0 – 200.0`
The BPM boundary where the engine's heuristics automatically activate to aggressively scan for alternating "strum-tap" patterns.
- **Low:** Starts penalising double-time readings at moderately slow speeds.
- **High:** Allows the engine to freely track fast tempos without heuristic interference.

**Double-Time Jump Tolerance** · default `20` · range `5 – 40`
Confidence delta required before the engine accepts a sudden doubling of BPM (e.g. 60 → 120).
- **Low:** BPM can jump to double speed with little evidence.
- **High:** Engine demands a very strong signal before accepting a 2× jump.

**Half-Time Jump Tolerance** · default `10` · range `5 – 30`
Confidence delta required before the engine accepts a sudden halving of BPM (e.g. 120 → 60).
- **Low:** BPM can drop to half speed with little evidence.
- **High:** Engine demands a very strong signal before accepting a ½× drop.

**Hard Reset Threshold** · default `20` · range `5 – 50`
The BPM difference required to bypass the engine's visual smoothing.
- **Low:** The display will instantly snap to almost any moderate tempo change.
- **High:** The engine relies heavily on smoothing, forcing the display to glide slowly even during intentional tempo shifts.

**Warmup Hops** · default `50` · range `10 – 250`
The minimum amount of audio history the engine must silently collect before it turns on the tempo estimator.
- **Low:** Outputs a BPM almost instantly, but the initial reading may be erratic.
- **High:** Forces the engine to wait and accumulate a mathematically solid pattern.

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

**Confidence Threshold** · default `0.50` · range `0.0 – 1.0`
The minimum dynamic programming score (based on alignment, stability, and coverage) required before the engine sends an updated BPM to the visual display.
- **Low:** The UI updates constantly even if the audio is messy, ambiguous, or pure background noise.
- **High:** The UI only updates when the rhythmic pulse is mathematically undeniable.

---

## Known Limitations
* BPM of Legato instruments may take a few seconds to intially recognise (up to 4 seconds).
* Complex rhythms (like 16th-note funk patterns) may still briefly trick the engine until hysteresis lock corrects it.
* Note value switching flushes the DSP to prevent buffer maths errors. This means the engine will be temporarily unavailable (~0.25s or 50 hops) while it rebuilds the novelty curve.


## Authors
* Manmohan Gahir
* Vincent Wang