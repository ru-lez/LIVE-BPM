# LiveBPM — Technical Documentation

> Real-time tempo (BPM) detection engine for live audio, by **Muse Scene Lab**.
> Authors: Manmohan Gahir, Vincent Wang.

This document is the in-depth reference for the LiveBPM codebase. It explains the
architecture, every stage of the DSP pipeline (with the actual maths and the
constants used in the code), the full configuration surface, both frontends, the
build system, and the test suite. Wherever a claim is tied to a specific piece of
code, the source file and line are referenced so the document can be verified
against the implementation.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Start](#2-quick-start)
3. [Project Structure & Build Targets](#3-project-structure--build-targets)
4. [Architecture at a Glance](#4-architecture-at-a-glance)
5. [The Processing Pipeline in Depth](#5-the-processing-pipeline-in-depth)
   - [5.1 Audio ingestion & the ring buffer](#51-audio-ingestion--the-ring-buffer)
   - [5.2 STFT — spectral analysis](#52-stft--spectral-analysis)
   - [5.3 Onset detection (dual stream)](#53-onset-detection-dual-stream)
   - [5.4 Adaptive gating, fusion & compression](#54-adaptive-gating-fusion--compression)
   - [5.5 Silence handling & the novelty buffer](#55-silence-handling--the-novelty-buffer)
   - [5.6 Tempo estimation](#56-tempo-estimation)
   - [5.7 The dynamic window lock](#57-the-dynamic-window-lock)
   - [5.8 Alternation heuristics](#58-alternation-heuristics)
   - [5.9 Hysteresis & commit](#59-hysteresis--commit)
   - [5.10 Beat tracking & the confidence score](#510-beat-tracking--the-confidence-score)
   - [5.11 Output scaling to master tempo](#511-output-scaling-to-master-tempo)
6. [Note Resolution Modes](#6-note-resolution-modes)
7. [Configuration Reference](#7-configuration-reference)
8. [Instrument Presets](#8-instrument-presets)
9. [Engine C++ API](#9-engine-c-api)
10. [CLI Frontend](#10-cli-frontend)
11. [Browser (WASM) Frontend](#11-browser-wasm-frontend)
12. [Build System & WASM Flags](#12-build-system--wasm-flags)
13. [Testing](#13-testing)
14. [Performance, Threading & Real-Time Safety](#14-performance-threading--real-time-safety)
15. [Known Limitations & Caveats](#15-known-limitations--caveats)
16. [Glossary](#16-glossary)

---

## 1. Overview

LiveBPM accepts a stream of mono audio samples, continuously analyses the
rhythmic structure, and outputs a stable BPM estimate together with a confidence
score in `[0, 1]`. The numerical core is written in portable C++17 and is shared
by two frontends:

- **CLI** — a native terminal application that captures microphone input through
  [miniaudio](https://github.com/mackron/miniaudio).
- **Browser (WASM)** — the same core compiled to WebAssembly with Emscripten and
  driven from an `AudioWorklet`.

The design principle is a strict separation of concerns: **the engine is 100%
analytical and emits raw, precise numbers; the frontends own all cosmetic
smoothing** (EMA, magnetic snapping, deadzone, confidence gating). This is why
parameters such as `uiSmoothing`, `VISUAL_DEADZONE`, `magneticSnapRange` and
`confThreshold` live in `Config` but are deliberately *not* applied by the engine
([Engine.cpp:689](livebpm/core/src/Engine.cpp:689)).

### Key signal constants (at the default 48 kHz)

| Quantity | Symbol in code | Value | Derivation |
|---|---|---|---|
| Sample rate | `sampleRate` | 48000 Hz | config default |
| FFT size | `fftSize` | 2048 | `1 << fftOrder`, `fftOrder = 11` |
| Hop size | `hopSize` | 512 | `fftSize / 4` (75 % overlap) |
| FFT bins | `numBins` | 1025 | `fftSize / 2 + 1` |
| Feature (hop) rate | `featureRate` | 93.75 Hz | `sampleRate / hopSize` |
| Hop period | — | ≈ 10.67 ms | `1 / featureRate` |
| Novelty buffer length | `novelty.maxLength` | 375 frames | `analysisWindowSeconds × featureRate` = `4.0 × 93.75` |
| Semitone bands | `numSemitoneBands` | ≈ 86 | `round(12·log2(8000/55))` |

These are derived in the engine constructor at
[Engine.cpp:143-182](livebpm/core/src/Engine.cpp:143).

---

## 2. Quick Start

### Native build + CLI

```bash
mkdir build && cd build
cmake ..
cmake --build .
./livebpm_cli            # add -r <samplerate> -b <blocksize> to override
```

### Browser build (WebAssembly)

```bash
# one-time: install & activate the Emscripten SDK (emsdk)
mkdir build-wasm && cd build-wasm
emcmake cmake ..
cmake --build .
# copy the web shell next to the generated artefacts, then serve
cp ../wasm/processor.js ../wasm/index.html .
python -m http.server 8080      # open http://localhost:8080
```

The build emits `livebpm_wasm.js` (the ES6 loader) and `livebpm_wasm.wasm`
(the binary). `index.html` fetches the `.wasm` itself, so all three files
(`index.html`, `processor.js`, `livebpm_wasm.js` + `.wasm`) must be served from
the same directory.

### Tests

```bash
cd build
ctest --output-on-failure
```

---

## 3. Project Structure & Build Targets

```
MSL-MEHEARSAL-LIVEBPM/
├── livebpm/
│   ├── core/                       # Engine API + presets
│   │   ├── include/livebpm/        # Config.h, Engine.h, Result.h, Presets.h
│   │   └── src/Engine.cpp          # the orchestrator (state machine + pipeline)
│   ├── dsp/                        # standalone DSP modules
│   │   ├── include/livebpm/dsp/    # headers
│   │   └── src/                    # SpectralAnalysis, OnsetDetection,
│   │                               #   TempoEstimation, BeatTrackingDP
│   ├── cli/main.cpp                # native frontend (miniaudio)
│   ├── wasm/                       # browser frontend
│   │   ├── bridge.cpp              # Embind C++↔JS wrapper
│   │   ├── processor.js            # AudioWorklet (audio thread)
│   │   └── index.html             # UI + main thread glue
│   ├── extern/kissfft/             # KissFFT (vendored)
│   ├── extern/miniaudio/           # miniaudio (vendored, CLI only)
│   ├── tests/                      # Catch2 unit + integration tests
│   └── CMakeLists.txt
├── emsdk/                          # Emscripten SDK (for the WASM build)
├── README.md
└── DOCUMENTATION.md                # (this file)
```

### Build targets ([CMakeLists.txt](livebpm/CMakeLists.txt))

| Target | Kind | Sources | Links |
|---|---|---|---|
| `kissfft` | static lib | `extern/kissfft/kiss_fft.c` | — |
| `livebpm_dsp` | static lib | the four `dsp/src/*.cpp` | `kissfft` |
| `livebpm_core` | static lib | `core/src/Engine.cpp` | `livebpm_dsp` |
| `livebpm_cli` | executable | `cli/main.cpp` | `livebpm_core` (+ miniaudio headers; `pthread dl m` on Linux) |
| `livebpm_wasm` | `.js` + `.wasm` | `wasm/bridge.cpp` | `livebpm_core` (Emscripten only) |
| `livebpm_tests` | executable | `tests/unit/*.cpp` | `livebpm_core`, `Catch2::Catch2WithMain` |

Catch2 v3.5.0 is pulled in via CMake `FetchContent`. The project compiles with
strict warnings (`/W4` on MSVC, `-Wall -Wextra -Wpedantic` elsewhere); third-party
headers are wrapped in `#pragma GCC diagnostic` guards so vendor code does not
break the strict build.

---

## 4. Architecture at a Glance

```
                       float samples (mono)
                                │
                                ▼
                        ┌───────────────┐
                        │  RingBuffer   │  (capacity 2·fftSize)
                        └───────────────┘
                                │  per hop (every 512 samples)
                                ▼
                 ┌──────────────────────────────┐
                 │  Hann window + KissFFT (2048) │ → magnitude[1025], phase[1025]
                 └──────────────────────────────┘
                          │                    │
            ┌─────────────┘                    └──────────────┐
            ▼                                                  ▼
   Stream 1: SuperFlux                              Stream 2: Complex Domain
   (semitone filter bank,                           (magnitude + phase
    half-wave rectified flux)                        Euclidean distance)
            │                                                  │
            └──────────► adaptive per-stream gate ◄────────────┘
                                │
                  weighted fusion + log1p compression
                                │
                                ▼
                    Novelty buffer (rolling ~4 s, 375 frames)
                                │
            ┌───────────────────┴───────────────────┐
            ▼                                         ▼
   TempoEstimation                            BeatTrackingDP
   (FFT autocorrelation,                      (dynamic programming over
    log-Gaussian bias,                         the novelty → beat grid →
    breakthrough, parabolic                    stability / alignment / coverage)
    interpolation)
            │                                         │
            ▼                                         ▼
   Dynamic window lock ──► alternation heuristics ──► hysteresis & commit
                                │
                                ▼
                  Result { bpm, confidence, hasValidEstimate, processingTimeMs }
                                │
                                ▼
            Frontend cosmetic layer (EMA + magnetic snap + deadzone + conf gate)
                                │
                                ▼
                            displayed BPM
```

The per-hop work (FFT → onset → novelty) runs inside a `while` loop in
`processBlock`; the tempo/beat/commit stage runs **once per `processBlock`
call**, after the hop loop drains
([Engine.cpp:568-655](livebpm/core/src/Engine.cpp:568)).

---

## 5. The Processing Pipeline in Depth

The orchestration lives entirely in `Engine::Impl`. All DSP state is grouped into
small structs (`fft`, `filterBank`, `gating`, `novelty`, `tempo`) so the data flow
is easy to follow ([Engine.cpp:80-141](livebpm/core/src/Engine.cpp:80)).

### 5.1 Audio ingestion & the ring buffer

`processBlock(samples, numSamples)` first writes the incoming block into a
lock-free `RingBuffer<float>` of capacity `2 × fftSize` and advances a hop
counter ([Engine.cpp:572](livebpm/core/src/Engine.cpp:572)).

`RingBuffer` ([RingBuffer.h](livebpm/dsp/include/livebpm/dsp/RingBuffer.h)) is a
simple modular-arithmetic circular buffer:

- `write()` drops the oldest samples if a single write exceeds capacity (keeps
  only the most recent `capacity` samples).
- `readLatest(out, count)` copies the most recent `count` samples in chronological
  order — this is how each analysis window is taken.

A hop is processed only when **both** conditions hold
([`hasEnoughDataForAnalysis`, Engine.cpp:208](livebpm/core/src/Engine.cpp:208)):

```
novelty.hopCounter >= hopSize   &&   ringBuffer.available() >= fftSize
```

Each hop decrements `hopCounter` by `hopSize` and reads the latest `fftSize`
samples, giving 75 % overlap between consecutive FFT windows.

> Because the CLI uses a 512-sample block (= one hop) and the browser uses the
> 128-sample AudioWorklet quantum (= a quarter hop), the CLI produces one hop per
> call while the browser accumulates four `process()` calls per hop. The
> `featureRate` is identical (93.75 Hz) in both.

### 5.2 STFT — spectral analysis

`SpectralAnalysis::compute`
([SpectralAnalysis.cpp:41](livebpm/dsp/src/SpectralAnalysis.cpp:41)) applies a
pre-computed **Hann window** to the 2048-sample frame, runs a real→complex FFT via
KissFFT, and writes:

- `magnitude[k] = sqrt(reᵏ² + imᵏ²)`
- `phase[k] = atan2(imᵏ, reᵏ)`

for the first `numBins = fftSize/2 + 1 = 1025` bins. The window is built once in
the constructor as `0.5·(1 − cos(2πi/(N−1)))`.

The engine also records `currentWindowMaxAmp` (peak absolute sample in the frame)
for the noise-gate decision later ([Engine.cpp:219-225](livebpm/core/src/Engine.cpp:219)).

### 5.3 Onset detection (dual stream)

Two complementary onset detectors run on every frame and are fused. Both live in
`OnsetDetection` ([OnsetDetection.cpp](livebpm/dsp/src/OnsetDetection.cpp)).

**Stream 1 — SuperFlux over a semitone filter bank** (Pertusa & Iñesta 2009).
A triangular filter bank maps the 1025 FFT bins onto ≈86 one-semitone bands
spanning A1 (55 Hz) to ≈B8 (8 kHz), built once at startup
([`buildSemitoneFilterBank`, OnsetDetection.cpp:108](livebpm/dsp/src/OnsetDetection.cpp:108)).
Per frame:

1. Band energy `bᵢ = sqrt(Σₖ (|X[k]|·Hᵢ[k])²)`.
2. **Maximum filter** over the previous frame's neighbouring bands
   (`±maxFilterWidth`, here 1) — this is what makes SuperFlux robust to vibrato and
   small pitch drift.
3. Half-wave-rectified flux `D = Σᵢ max(0, bᵢ − maxPrevᵢ)`.
4. Normalise by total band energy: `output = D / Σᵢ bᵢ`, bounded to roughly
   `[0, 1]`.

This stream is strong on percussive and clearly articulated material.

**Stream 2 — Complex domain** (Duxbury et al. 2003)
([`complexDomain`, OnsetDetection.cpp:22](livebpm/dsp/src/OnsetDetection.cpp:22)).
For each bin it *predicts* the current phase by linear extrapolation
(`2·prevPhase − prevPrevPhase`, wrapped to `[−π, π]` by `princarg`) and measures
the Euclidean distance between the predicted complex value and the measured one:

```
distSq = |X|² + |Xprev|² − 2·|X|·|Xprev|·cos(Δφ)
cd     = Σₖ sqrt(max(0, distSq))  / numBins
```

This catches **phase-only onsets** — bowed strings changing notes, sustained
vocals — that magnitude-only detectors miss. Its raw scale is amplitude-dependent,
so it is normalised before fusion.

### 5.4 Adaptive gating, fusion & compression

`calculateNoveltyFlux` ([Engine.cpp:231](livebpm/core/src/Engine.cpp:231)) gates
each stream against **its own** exponential moving average so the two detectors
adapt independently:

```
threshold_s = MA_s + onsetSensitivity
gated_s     = (flux_s > threshold_s) ? (flux_s − MA_s) : 0
MA_s        = gatingAlpha·flux_s + (1 − gatingAlpha)·MA_s     (same for complex)
```

The complex stream is then normalised by its moving average (`gated / (MA + ε)`,
clamped to 1.0) so both streams sit on a comparable scale. Fusion is a weighted
sum controlled by `complexDomainWeight`:

```
combined = (1 − w)·gated_semitone + w·normalisedComplex
```

During warm-up (`gating.hopsProcessed < warmupHops`) only the well-bounded
semitone stream is used, because the complex moving average is still unreliable.
Finally the value is **logarithmically compressed** to tame dynamic range:

```
novelty = log1p(fluxCompression · combined)
```

### 5.5 Silence handling & the novelty buffer

`updateSilenceAndGating` ([Engine.cpp:299](livebpm/core/src/Engine.cpp:299)):

- If `currentWindowMaxAmp < SILENCE_THRESHOLD`, the frame's novelty is forced to
  0 and a silence counter increments. After `silenceTimeoutSec × featureRate`
  consecutive silent hops the engine **bleeds** state: `currentStableBpm *= 0.95`
  per hop and the octave/confirmation counters reset. (This is a gentle decay, not
  a hard flush — a full flush only happens on `reset()`.)
- The (smoothed) novelty value is appended to a rolling buffer of length
  `maxLength = analysisWindowSeconds × featureRate ≈ 375`. Once full, the buffer
  shifts left by one (`std::move`) and appends at the back, keeping the most recent
  ~4 seconds. The per-frame peak amplitude is stored in a parallel
  `amplitudeBuffer` (used by the alternation heuristic).

A small EMA (`noveltySmoothing`) is applied to the raw novelty before storage to
round off jagged legato peaks.

`shiftBuffers` ([Engine.cpp:342](livebpm/core/src/Engine.cpp:342)) then rotates
the spectral history (band energies, magnitude, two frames of phase) for the next
hop and increments `hopsProcessed`.

### 5.6 Tempo estimation

After the hop loop, if `noveltyLen > warmupHops`, the engine estimates tempo
([Engine.cpp:586](livebpm/core/src/Engine.cpp:586)) via
`TempoEstimation::estimate` ([TempoEstimation.cpp:194](livebpm/dsp/src/TempoEstimation.cpp:194)).
The algorithm has five steps:

1. **Autocorrelation via FFT** (Wiener–Khinchin). The novelty is zero-padded to the
   next power of two ≥ `2·maxLength` (≈1024), forward-FFT'd, converted to a power
   spectrum `|FFT|²`, inverse-FFT'd, and normalised by lag 0
   ([TempoEstimation.cpp:66](livebpm/dsp/src/TempoEstimation.cpp:66)). This yields
   the autocorrelation of the novelty in `O(N log N)`.

2. **Lag evaluation with a center bias**
   ([TempoEstimation.cpp:95](livebpm/dsp/src/TempoEstimation.cpp:95)). Lags are
   scanned between `minLag = 60·featureRate/maxBpm` and
   `maxLag = 60·featureRate/minBpm`. Each lag's score is weighted by a
   **log-Gaussian** centred on the current stable BPM:

   ```
   weight = exp( −0.5 · ( log2(bpm / centerBpm) / dynamicWidth )² )
   dynamicWidth = 1.1 + 3.0·(1 − currentConfidence)
   ```

   The width *widens* when confidence is low, so a weak signal is not yanked toward
   the previous estimate. The estimator tracks both the biased winner and the
   strongest *raw* peak.

3. **Breakthrough logic** ([TempoEstimation.cpp:159](livebpm/dsp/src/TempoEstimation.cpp:159)).
   If the strongest raw peak is ≈2× the biased winner and its score is within 95 %
   of the biased winner's, the estimator switches to the raw peak. This lets a
   genuine double-time pattern "break through" the center bias. It is **disabled**
   in SmartMixed mode.

4. **Parabolic interpolation** around the winning lag for sub-bin precision
   ([TempoEstimation.cpp:172](livebpm/dsp/src/TempoEstimation.cpp:172)).

5. **Final BPM** = `60 · featureRate / refinedLag`.

**SmartMixed mode** ([TempoEstimation.cpp:116](livebpm/dsp/src/TempoEstimation.cpp:116))
augments step 2: it *folds* the 8th-note lag energy (`×0.5`) and 16th-note lag
energy (`×0.25`) into each quarter-note candidate's score, and — above 135 BPM —
multiplies a candidate's score by 0.5 if a strong half-time peak sits underneath
it. This biases the estimator toward a coherent master pulse without locking onto a
single subdivision.

### 5.7 The dynamic window lock

`estimateRawTempo` ([Engine.cpp:369](livebpm/core/src/Engine.cpp:369)) wraps the
estimator with a **lock** that, once engaged, clamps the search range to
`stableBpm × (1 ± dynamicWindowPercent)` — the single most effective defence
against half-/double-time octave jumps.

Lock state is governed by a **two-threshold hysteresis band** so that ordinary
mid-confidence wobble neither builds nor breaks the lock:

- **Not locked:** while `confidence ≥ lockEntryConfidence` and a stable BPM exists,
  a `windowLockTimer` counts up; after **300 hops (≈3.2 s)** the window locks. Any
  drop below the entry threshold resets the timer.
- **Locked:** while `confidence ≤ lockReleaseConfidence`, a `lowConfidenceStreak`
  counts up; after **300 hops (≈3.2 s)** the lock releases. Any reading above the
  release threshold resets the streak.
- **Dead band** (`lockReleaseConfidence < conf < lockEntryConfidence`): neither
  counter moves — the lock simply holds.

> **Note on timing.** The 300-hop thresholds are hardcoded
> ([Engine.cpp:391](livebpm/core/src/Engine.cpp:391),
> [Engine.cpp:402](livebpm/core/src/Engine.cpp:402)). At 93.75 hops/s that is
> ≈3.2 s.

A **breakthrough defence** also runs while locked: if the estimator still returns a
BPM outside the clamped window, it is overwritten with the (scaled) tracking BPM
([Engine.cpp:426](livebpm/core/src/Engine.cpp:426)).

### 5.8 Alternation heuristics

`applyAlternationHeuristics` ([Engine.cpp:435](livebpm/core/src/Engine.cpp:435))
runs **only when unlocked and `useHeuristics` is on**. It targets the classic
"strum-tap" failure where a fast guitar/strum pattern reads as double-time.

Above `fastTempoThreshold`, it samples the **amplitude buffer** at 8 positions one
beat apart and compares the summed peaks on even vs. odd beats. A persistent
even/odd imbalance (the smoothed ratio exceeding a speed-dependent threshold:
1.15 if slow, 1.4 neutral, 1.8 if already fast) indicates an alternating pattern,
so the BPM is **halved**. Otherwise a `highBpmConfirmationCount` (0–20) builds up as
evidence that the fast reading is real.

### 5.9 Hysteresis & commit

`applyHysteresisAndCommit` ([Engine.cpp:507](livebpm/core/src/Engine.cpp:507)) is
the final state machine that decides what `currentStableBpm` becomes. It has two
regimes:

**Locked — total freeze.** The only permitted movement is micro-drift within
`lockedDriftTolerance` (default 3 BPM). Anything further away is rejected outright
and the previous stable value is returned. Allowed drift is blended in with
`hysteresisBlend`. The lock can only be exited by the release condition in
§5.7 — no signal-based heuristic can break it.

**Unlocked — octave-aware glide.**
- If a candidate is within `doubleTimeJumpThreshold` of `2× stable` or within
  `halfTimeJumpThreshold` of `0.5× stable`, it is treated as an **octave error**:
  an `octaveMismatchCount` increments and the candidate is clamped to the stable
  value. Only after `octaveMismatchTolerance` consecutive octave errors does the
  engine concede and jump.
- Otherwise, if the change exceeds `hardResetThreshold` it **snaps** instantly;
  below that it glides via `hysteresisBlend` EMA.

### 5.10 Beat tracking & the confidence score

Independently of tempo, `BeatTrackingDP::track`
([BeatTrackingDP.cpp:11](livebpm/dsp/src/BeatTrackingDP.cpp:11)) lays down an
optimal beat grid over the novelty buffer using dynamic programming:

- A transition **penalty** `−penaltyFactor·log2(d/beatRefLag)²` discourages
  inter-beat intervals that stray from the expected lag (search range
  `[beatRefLag/2, beatRefLag·2]`, `penaltyFactor = 100`).
- A forward pass accumulates `cost[i] = novelty[i] + max_j(cost[j] + penalty)` and
  records back-links; backtracking from the global maximum yields the beat indices.

Three metrics are derived and combined into the confidence
([Engine.cpp:643](livebpm/core/src/Engine.cpp:643)):

```
confidence = 0.4·stability + 0.4·alignment + 0.2·coverage
```

| Metric | Definition | Meaning |
|---|---|---|
| **stability** | `1 − CV` of inter-beat intervals | how regular the beat spacing is |
| **alignment** | mean novelty at beats / max novelty | how well beats land on peaks |
| **coverage** | `(lastBeat − firstBeat) / length` | fraction of the window spanned |

`hasValidEstimate` is set when `confidence ≥ confThreshold`
([Engine.cpp:646](livebpm/core/src/Engine.cpp:646)).

### 5.11 Output scaling to master tempo

The note-resolution `inputScale` multiplies the search bounds and the center bias so
the estimator hunts at the requested subdivision. As soon as a raw BPM comes back it
is divided by `inputScale` to report the **master quarter-note tempo**
([Engine.cpp:604](livebpm/core/src/Engine.cpp:604)) — so regardless of the chosen
resolution, the displayed number is always in quarter-note BPM.
---

## 6. Note Resolution Modes

`NoteResolution` ([Config.h:6](livebpm/core/include/livebpm/Config.h:6)) tells the
engine what rhythmic layer to treat as "the beat". Internally each mode sets an
`inputScale` ([Engine.cpp:592-596](livebpm/core/src/Engine.cpp:592)); the reported
BPM is the detected pulse divided by that scale.

| Mode | `inputScale` (search) | Net effect on reported BPM (= 1 / scale) | Use case |
|---|---|---|---|
| `HalfNote` | 0.5 | ×2.0 | Ambient/doom; pulses span two beats |
| `QuarterNote` *(default)* | 1.0 | ×1.0 | Standard 4/4 pop/rock/electronic |
| `EighthNote` | 2.0 | ×0.5 | Driving 8th-note strumming / hats |
| `EighthNoteTriplet` | 3.0 | ×0.333 | Shuffle, blues, 6/8 & 12/8 |
| `SixteenthNote` | 4.0 | ×0.25 | Fast trap hats, dense fingerpicking |
| `SmartMixed` | 1.0 (+ folding) | auto | Syncopated grooves; engine folds subdivisions |

> **Note:** an eighth-note triplet packs three pulses into one quarter-note beat, so the
> detected triplet pulse is divided by 3 (`inputScale = 3.0`) to recover the master
> quarter-note tempo — a net `×0.333`.

Changing resolution at runtime calls `reset()`
([Engine.cpp:692](livebpm/core/src/Engine.cpp:692)), flushing the novelty curve;
the engine is therefore briefly unavailable (~50 hops, ≈0.5 s) while it rebuilds.

---

## 7. Configuration Reference

Every tunable lives in `livebpm::Config`
([Config.h](livebpm/core/include/livebpm/Config.h)). The browser exposes them as
Advanced-panel sliders; the CLI sets them through instrument presets.
`updateTunableConfig` ([Engine.cpp:667](livebpm/core/src/Engine.cpp:667)) pushes a
whole `Config` into a live engine (display-layer fields excepted).

### Fixed (construction time)

| Parameter | Default | Description |
|---|---|---|
| `sampleRate` | 48000 | Audio sample rate (Hz) |
| `blockSize` | 512 | Samples per `processBlock` (CLI 512, browser 128) |
| `fftOrder` | 11 | FFT size = `2^11` = 2048 |
| `analysisWindowSeconds` | 4.0 | Rolling novelty-buffer length |

### Onset detection

| Parameter | Default | Range | Description |
|---|---|---|---|
| `onsetSensitivity` | 0.015 | 0.005–0.1 | Energy jump above the per-stream MA needed to register an onset |
| `complexDomainWeight` | 0.4 | 0–1 | Fusion balance: percussive SuperFlux ↔ tonal complex domain |
| `fluxCompression` | 50.0 | 10–100 | `log1p` λ; higher flattens loud/quiet differences |
| `noveltySmoothing` | 0.4 | 0.05–0.9 | EMA alpha on the novelty curve |

### Noise & gating

| Parameter | Default | Range | Description |
|---|---|---|---|
| `SILENCE_THRESHOLD` | 0.03 | 0–0.1 | Peak-amplitude floor below which a frame is "silence" |
| `gatingAlpha` | 0.1 | 0.01–0.5 | Adaptation speed of the per-stream noise gate |
| `silenceTimeoutSec` | 2.0 | 0.5–5 | Continuous silence before state begins to bleed |

### Tempo tracking & lock

| Parameter | Default | Range | Description |
|---|---|---|---|
| `minBpm` | 40 | 30–120 | Lower search bound |
| `maxBpm` | 240 | 120–480 | Upper search bound |
| `dynamicWindowPercent` | 0.40 | 0.10–1.0 | Half-width of the locked search window (±%) |
| `lockEntryConfidence` | 0.75 | 0.50–0.95 | Confidence needed to build toward a lock |
| `lockReleaseConfidence` | 0.60 | 0.10–0.70 | Confidence below which the release counter runs |
| `lockedDriftTolerance` | 3.0 | 0–10 | Max BPM drift accepted while locked |
| `hysteresisBlend` | 0.05 | 0.01–0.2 | EMA alpha for stable-BPM glide / micro-drift |
| `doubleTimeJumpThreshold` | 20.0 | 5–40 | Proximity to `2× stable` counted as an octave error |
| `halfTimeJumpThreshold` | 10.0 | 5–30 | Proximity to `0.5× stable` counted as an octave error |
| `octaveMismatchTolerance` | 100 | 10–300 | Consecutive octave errors before conceding the jump |
| `hardResetThreshold` | 20.0 | 5–50 | Non-octave change that snaps instantly (unlocked) |
| `fastTempoThreshold` | 140.0 | 100–200 | BPM above which alternation heuristics activate |
| `useHeuristics` | true | bool | Master switch for alternation/octave correction |
| `warmupHops` | 50 | 10–250 | Hops collected before tempo estimation starts |

### Display (set in `Config`, applied by the frontends — not the engine)

| Parameter | Default | Range | Description |
|---|---|---|---|
| `uiSmoothing` | 0.15 | 0.01–0.5 | EMA alpha on the displayed BPM |
| `VISUAL_DEADZONE` | 0.1 | 0–5 | Minimum change before the display updates |
| `magneticSnapRange` | 0.15 | 0–0.5 | Snap radius toward whole numbers (127.9 → 128) |
| `confThreshold` | 0.50 | 0.20–0.95 | Min confidence for `hasValidEstimate` / display update |

---

## 8. Instrument Presets

`GetPreset(name)` ([Presets.h](livebpm/core/include/livebpm/Presets.h)) returns a
fully tuned `Config`. Ten presets ship (the base defaults double as "default").
Selected values that differ most from the defaults:

| Preset | Heuristics | cdWeight | onsetSens | minBpm | maxBpm | dynWin% | octTol | confThr | Notes |
|---|---|---|---|---|---|---|---|---|---|
| `default` | on | 0.40 | 0.015 | 40 | 240 | 0.40 | 100 | 0.50 | Balanced general purpose |
| `cajon` | on | 0.20 | 0.025 | 40 | 280 | 0.50 | 30 | 0.65 | Percussive; fast/reactive, wide range |
| `flute` | on | 0.75 | 0.010 | 50 | 200 | 0.30 | 100 | 0.60 | Legato; complex-domain heavy |
| `guitar` | on | 0.35 | 0.018 | 40 | 240 | 0.40 | 60 | 0.55 | Acoustic guitar |
| `electricguitar` | on | 0.20 | 0.015 | 40 | 240 | 0.40 | 60 | 0.60 | Electric guitar |
| `acousticbass` | on | 0.50 | 0.020 | 40 | 200 | 0.25 | 200 | 0.55 | Tight window, very sticky octave |
| `electricbass` | on | 0.30 | 0.025 | 60 | 200 | 0.30 | 200 | 0.60 | Electric bass |
| `saxophone` | on | 0.55 | 0.012 | 40 | 240 | 0.35 | 100 | 0.60 | Alto/tenor sax |
| `liveband` | on | 0.50 | 0.030 | 50 | 220 | 0.45 | 40 | 0.55 | Full mix; higher silence floor |
| `slowacoustic` | **off** | 0.75 | 0.005 | 30 | 120 | 0.25 | 150 | 0.45 | Ambient; heuristics disabled |

(Each preset also tunes `gatingAlpha`, `noveltySmoothing`, `fluxCompression`,
`hysteresisBlend`, `uiSmoothing`, `VISUAL_DEADZONE`, `magneticSnapRange`,
`fastTempoThreshold`, `silenceTimeoutSec`, `warmupHops`, `hardResetThreshold` and
the octave jump thresholds — see the source for exact figures.)

**Where presets are selected:** the CLI maps numeric IDs 0–9 to these names
([main.cpp:147-161](livebpm/cli/main.cpp:147)); the browser passes the name string
to `loadPreset`, which both applies it to the engine and returns the values so the
UI sliders can sync ([bridge.cpp:67](livebpm/wasm/bridge.cpp:67)).

---

## 9. Engine C++ API

```cpp
#include "livebpm/Engine.h"

livebpm::Config cfg;
cfg.sampleRate = 48000.0;
livebpm::Engine engine(cfg);

// Feed audio (call from one thread only — the audio callback)
engine.processBlock(samples, numSamples);

// Read the last computed result (safe from another thread; returns a copy)
livebpm::Result r = engine.getLatestResult();
//   r.bpm, r.confidence, r.hasValidEstimate, r.processingTimeMs

// Runtime control
engine.setTargetResolution(livebpm::NoteResolution::QuarterNote); // flushes state
engine.updateTunableConfig(newConfig);                            // bulk update
engine.setComplexDomainWeight(0.6);                               // individual setters
engine.reset();                                                   // flush everything
```

- The class uses the **PIMPL idiom** ([Engine.h:52](livebpm/core/include/livebpm/Engine.h:52)):
  all DSP state hides behind `std::unique_ptr<Impl>`, keeping the public header
  dependency-free.
- `setTargetResolution` and `setHeuristicsEnabled` call `reset()` internally,
  because changing either mid-stream would corrupt the rolling buffers
  ([Engine.cpp:692](livebpm/core/src/Engine.cpp:692),
  [Engine.cpp:699](livebpm/core/src/Engine.cpp:699)).
- `Result` ([Result.h](livebpm/core/include/livebpm/Result.h)) is a plain struct:
  `bpm`, `confidence`, `hasValidEstimate`, `processingTimeMs`.
- **Thread-safety:** `processBlock` is not re-entrant — call it from a single audio
  thread. `getLatestResult` returns a value copy and is safe to poll from a UI
  thread.

---

## 10. CLI Frontend

Source: [cli/main.cpp](livebpm/cli/main.cpp).

```
livebpm_cli [-r samplerate] [-b blocksize] [-h]
```

**Startup flow:** enumerate capture devices → pick a device → pick an instrument
preset (0–9) → pick a note resolution (1–6) → start capture. Capture is configured
as mono `f32` at the requested rate with a period of `blockSize` frames
([main.cpp:328-334](livebpm/cli/main.cpp:328)).

**Pre-filtering.** Before audio reaches the engine, the callback runs it through an
80 Hz Biquad **high-pass** to strip DC offset and low-frequency rumble
([main.cpp:50-71](livebpm/cli/main.cpp:50)). The filter is built from the
`Biquad::makeHighpass` helper ([Biquad.h:30](livebpm/dsp/include/livebpm/dsp/Biquad.h:30)).

**Live controls** (handled on a dedicated input thread,
[main.cpp:195](livebpm/cli/main.cpp:195)):

| Input | Action |
|---|---|
| `1`–`6` | change note resolution |
| `preset N` (0–9) | switch instrument preset |
| `Ctrl+C` | stop (SIGINT handler flips the `running` flag) |

**Display loop** runs at 10 fps ([main.cpp:362](livebpm/cli/main.cpp:362)). It pulls
the latest result, applies the active preset's `uiSmoothing` (EMA), magnetic
snapping, and visual deadzone, then renders a single rewriting line with BPM,
confidence %, processing time, resolution and preset. The CLI uses its own
`CONF_THRESHOLD = 0.45` gate for the display
([main.cpp:359](livebpm/cli/main.cpp:359)).

---

## 11. Browser (WASM) Frontend

Three pieces cooperate across two threads:

```
 main thread (index.html)                 audio thread (processor.js)
 ───────────────────────────              ────────────────────────────
 fetch('livebpm_wasm.wasm')               AudioWorkletProcessor
   → arrayBuffer  ───────────[init msg]──►  createLiveBpmModule({wasmBinary})
 getUserMedia → MediaStreamSource           new LiveBpmEngine(sr, 128)
   → AudioWorkletNode                        _malloc(128*4)  (reused heap block)
                                            process(): copy 128 floats into heap,
 handleWorkletMessage  ◄──[result every 10]─  engine.processBlock(ptr,128)
   EMA + magnetic snap + deadzone            engine.getResult() → postMessage
   → DOM update (requestAnimationFrame)
```

**Why the binary is transferred as an `ArrayBuffer`.** The main thread fetches the
`.wasm` itself and hands the bytes to the worklet in the `init` message
([index.html:551-563](livebpm/wasm/index.html:551)); the worklet instantiates the
module from those bytes (`createLiveBpmModule({ wasmBinary })`,
[processor.js:72](livebpm/wasm/processor.js:72)). This sidesteps the browser's
module-fetch/caching path inside the AudioWorklet scope.

**Hot path.** `process()` copies the 128-sample quantum into a **pre-allocated**
WASM heap block (no per-callback `malloc`) and calls `processBlock`
([processor.js:90-119](livebpm/wasm/processor.js:90)). To avoid saturating the
message port, results are posted to the main thread only **every 10th** call
(~13 ms cadence).

**C++↔JS bridge.** `bridge.cpp` ([bridge.cpp](livebpm/wasm/bridge.cpp)) is a thin
Embind wrapper (`LiveBpmEngine`) exposing `processBlock`, `getResult`,
`setTargetResolution`, `loadPreset`, `reset`, and one setter per tunable. The
`NoteResolution` enum is registered for JS too.

**Message protocol** (main → worklet): `init`, `loadPreset`, `setResolution`,
`setHeuristics`, `setParameter` (generic — maps `param` → `set<Param>` method),
`reset`. Worklet → main: `ready` (carries the engine's visual deadzone),
`presetLoaded` (carries the resolved config so sliders can sync), `result`,
`error` ([processor.js:23-66](livebpm/wasm/processor.js:23)).

**Cosmetic layer.** The browser applies its own EMA (`alpha`), magnetic snap and
deadzone in the render loop on the main thread
([index.html:798-804](livebpm/wasm/index.html:798)), keeping the engine output raw.
The **Advanced panel** ([index.html:468+](livebpm/wasm/index.html:468)) exposes the
full parameter set as sliders that round-trip through the generic `setParameter`
message.

---

## 12. Build System & WASM Flags

The WASM target is only added when configured through `emcmake`
(`if (EMSCRIPTEN)` in [CMakeLists.txt](livebpm/CMakeLists.txt)). The link flags
worth knowing:

| Flag | Purpose |
|---|---|
| `--bind` | Enable Embind (C++ classes/enums callable from JS) |
| `-s MODULARIZE=1 -s EXPORT_ES6=1 -s EXPORT_NAME=createLiveBpmModule` | Emit an ES6 module factory |
| `-s ENVIRONMENT=web,worker,worklet` | Allow loading inside an AudioWorklet |
| `-s ALLOW_MEMORY_GROWTH=1` | Heap can grow at runtime |
| `-s EXPORTED_FUNCTIONS=_malloc,_free` | JS can allocate the reused audio block |
| `-s EXPORTED_RUNTIME_METHODS=HEAPF32` | JS gets a typed view onto the heap |
| `-O2` | Optimise |

All native targets also compile with `-matomics -mbulk-memory` under Emscripten so
WebAssembly shared-memory instructions are available.

---

## 13. Testing

Catch2 unit + integration tests live in `tests/unit` and are discovered
automatically via `catch_discover_tests`. Run with `ctest --output-on-failure`.

| File | Coverage |
|---|---|
| [test_SpectralAnalysis.cpp](livebpm/tests/unit/test_SpectralAnalysis.cpp) | FFT correctness: pure-sine peak bin, silence ≈ 0, DC → bin 0 only, impulse spreads across bins, allocation/size invariants |
| [test_OnsetDetection.cpp](livebpm/tests/unit/test_OnsetDetection.cpp) | Complex-domain ground truth (steady state, phase jump, magnitude step), bin-count normalisation, filter-bank dimensions, SuperFlux positivity |
| [test_TempoEstimation.cpp](livebpm/tests/unit/test_TempoEstimation.cpp) | Synthetic 60/120/180 BPM novelties, silence → weak result, min/max bounding, degenerate short input |
| [test_BeatTrackingDP.cpp](livebpm/tests/unit/test_BeatTrackingDP.cpp) | Periodic novelty → regular beats, peak alignment, empty/single-sample safety, coverage metric, penalty-factor robustness |
| [test_RingBuffer.cpp](livebpm/tests/unit/test_RingBuffer.cpp) | `clear`, single-pass overflow, over-read clamping |
| [test_FullPipeline.cpp](livebpm/tests/unit/test_FullPipeline.cpp) | End-to-end 120 BPM click track through the `Engine`; `reset()` clears state |

Shared synthetic-signal generators are in
[tests/helpers/TestSignals.h](livebpm/tests/helpers/TestSignals.h).

---

## 14. Performance, Threading & Real-Time Safety

- **Cost per hop** is dominated by one 2048-point FFT plus the ≈86-band filter-bank
  pass; per `processBlock` there is one additional ≈1024-point FFT pair for the
  autocorrelation and an `O(N²)`-bounded DP over ≤375 novelty samples (the DP search
  is constrained to `[beatRefLag/2, beatRefLag·2]`, so it is far below the worst
  case). `processingTimeMs` on `Result` reports the measured wall-clock cost of each
  call.
- **Allocation discipline.** All large buffers are sized once in the constructor;
  the browser pre-allocates its single heap block. The novelty buffer's
  full-buffer `std::move` shift on each hop is the main per-hop copy.
- **Latency floor.** Tempo estimation only begins after `warmupHops`, and the lock
  needs ≈3.2 s of sustained confidence to engage. Legato material can take up to the
  full ~4 s analysis window to settle.
- **Threading model.** One audio/producer thread calls `processBlock`; UI/consumer
  threads only ever call `getLatestResult` (value copy). The CLI additionally runs a
  separate stdin thread for live commands. There are no locks on the hot path.

---

## 15. Known Limitations & Caveats

- **Legato onset latency.** Bowed/sustained sources can take several seconds to lock
  because the complex-domain stream needs its moving average to warm up and the
  novelty buffer to fill.
- **Brief octave confusion.** Dense 16th-note funk and similar patterns can read as
  double-time for a moment until hysteresis/heuristics correct it.
- **Resolution/heuristics switching flushes the engine.** Both call `reset()`, so
  the detector is unavailable for ~50 hops (≈0.5 s) while it rebuilds.
- **CLI high-pass corner frequency** is fixed at 80 Hz regardless of sample rate, which is the intended cutoff. The filter coefficients are computed using the actual negotiated device sample rate ([main.cpp:57](livebpm/cli/main.cpp:57)).

---

## 16. Glossary

| Term | Meaning |
|---|---|
| **Hop** | One analysis step; the FFT window advances by `hopSize` (512) samples each hop |
| **Feature rate** | Hops per second (93.75 Hz at 48 kHz / 512) — the time base for all BPM↔lag conversions |
| **Novelty curve** | Per-hop onset-strength signal; its periodicity encodes tempo |
| **SuperFlux** | Onset detector using a maximum-filtered, half-wave-rectified spectral flux over semitone bands |
| **Complex domain** | Onset detector measuring complex-plane distance between predicted and observed FFT bins |
| **Autocorrelation** | Self-similarity of the novelty across lags; computed here via FFT (Wiener–Khinchin) |
| **Lag** | Sample/frame offset; `BPM = 60 · featureRate / lag` |
| **Lock / dynamic window** | State in which the search range is clamped to ±`dynamicWindowPercent` around the stable BPM |
| **Octave error** | A half-time or double-time misread of the true tempo |
| **Hysteresis** | Resistance to change that holds the stable BPM until evidence is strong/persistent |
| **Center bias** | Log-Gaussian weighting that favours tempos near the current stable BPM |

---

*Generated against the source on branch `manmohan-gahir/livebpm`. File and line
references point at the implementation so this document can be checked directly
against the code.*
