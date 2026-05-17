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
 *   First, the FFT magnitues and phases for each audio block are calculated. 
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
 * - Stablising Readings (Post-Processing & Hysteresis)
 *   Heuristics are applied as raw tempo maths can be jumpy. The engine looks for strumming or
 *   tapping patterns by analysing amplitude peaks, which helps prevent accidental double-time
 *   BPM readings. A Hysteresis lock is also implemented where if the audio signal is muddy or
 *   ambiguous, the engine holds onto the last stable BPM instead of jumping to half-time or
 *   double-time.
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
 */

 

#include "livebpm/Engine.h"
#include "livebpm/dsp/SpectralAnalysis.h"
#include "livebpm/dsp/OnsetDetection.h"
#include "livebpm/dsp/TempoEstimation.h"
#include "livebpm/dsp/BeatTrackingDP.h"
#include "livebpm/dsp/RingBuffer.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <vector>
#include <cmath>

namespace livebpm {

struct Engine::Impl {
    // Magic numbers
    static constexpr float GATING_OFFSET = 0.015f;
    static constexpr float GATING_ALPHA = 0.1f;
    static constexpr float FLUX_COMPRESSION_LAMBDA = 50.0f;
    static constexpr double FAST_TEMPO_THRESHOLD = 140.0;
    static constexpr double SILENCE_TIMEOUT_SEC = 2.0;
    static constexpr int WARMUP_HOPS = 50;

    Config config;

    // FFT and buffer state
    struct FFTState {
        int fftSize;
        int hopSize;
        int numBins;
        double featureRate;
        std::vector<float> window;
        std::vector<float> currentMag;
        std::vector<float> previousMag;          // for complex domain
        std::vector<float> phase;
        std::vector<float> previousPhase;        // for complex domain
        std::vector<float> previousPreviousPhase; // for complex domain
        float currentWindowMaxAmp = 0.0f;
    } fft;

    // Semitone filter bank state
    struct FilterBankState {
        std::vector<float> filterBank;
        std::vector<float> currentBandEnergies;
        std::vector<float> previousBandEnergies;
        int numSemitoneBands = 0;
    } filterBank;

    // Adaptive gating state
    struct GatingState {
        // Adaptive gating - independent moving averages per stream
        float semitoneFluxMovingAverage = 0.0f;
        float complexFluxMovingAverage = 0.0f;
        float noveltyMovingAverage = 0.0f;
        // Warm-up counter so we don't fuse with garbage moving averages
        int hopsProcessed = 0;
    } gating;

    // Novelty and amplitude tracking
    struct NoveltyState {
        std::vector<float> noveltyBuffer;
        std::vector<float> amplitudeBuffer;
        int hopCounter = 0;
        int writePos = 0;
        int maxLength;
    } novelty;

    // Tempo tracking and hysteresis state
    struct TempoState {
        float smoothedAlternationRatio = 1.0f;
        // Tempo hysteresis: hold the last stable BPM to prevent erratic jumps when the signal is weak or ambiguous
        double currentStableBpm = 0.0;
        int octaveMismatchCount = 0;
        int highBpmConfirmationCount = 0;
        int consecutiveSilenceHops = 0;
    } tempo;

    dsp::RingBuffer<float> ringBuffer;
    dsp::SpectralAnalysis spectral;
    dsp::TempoEstimation tempoEstimator;
    dsp::BeatTrackingDP beatTracker;

    Impl(const Config& cfg)
        : config(cfg),
          ringBuffer((1 << cfg.fftOrder) * 2),
          spectral(1 << cfg.fftOrder),
          tempoEstimator(static_cast<int>(cfg.analysisWindowSeconds * (static_cast<double>(cfg.sampleRate) / ((1 << cfg.fftOrder) / 4)))),
          beatTracker()
    {
        fft.fftSize = 1 << cfg.fftOrder;
        fft.hopSize = fft.fftSize / 4;
        fft.numBins = fft.fftSize / 2 + 1;
        fft.featureRate = static_cast<double>(cfg.sampleRate) / fft.hopSize;

        fft.window.resize(fft.fftSize, 0.0f);
        fft.currentMag.resize(fft.numBins, 0.0f);
        fft.previousMag.resize(fft.numBins, 0.0f);
        fft.phase.resize(fft.numBins, 0.0f);
        fft.previousPhase.resize(fft.numBins, 0.0f);
        fft.previousPreviousPhase.resize(fft.numBins, 0.0f);

        // Build filter bank once at startup
        filterBank.filterBank = dsp::OnsetDetection::buildSemitoneFilterBank(
            fft.numBins,
            static_cast<float>(cfg.sampleRate),
            fft.fftSize,
            55.0f,    // A1 lower bound
            8000.0f   // ~B8 upper bound
        );

        filterBank.numSemitoneBands = static_cast<int>(filterBank.filterBank.size()) / fft.numBins;
        filterBank.currentBandEnergies.resize(filterBank.numSemitoneBands, 0.0f);
        filterBank.previousBandEnergies.resize(filterBank.numSemitoneBands, 0.0f);

        novelty.maxLength = static_cast<int>(cfg.analysisWindowSeconds * fft.featureRate);
        novelty.noveltyBuffer.resize(novelty.maxLength, 0.0f);
        novelty.amplitudeBuffer.resize(novelty.maxLength, 0.0f);

        std::cout << "[Engine] Initialised semitone filter bank: "
                  << filterBank.numSemitoneBands << " bands\n";
        std::cout << "[Engine] Onset detection: semitone flux + complex domain (fused)\n";
    }

    // Helper methods
    void flushTrackingState() {
        // Flush the hysteresis and tracking state
        tempo.currentStableBpm = 0.0;
        tempo.octaveMismatchCount = 0;
        tempo.highBpmConfirmationCount = 0;
        tempo.smoothedAlternationRatio = 1.0f;
        tempo.consecutiveSilenceHops = 0;

        // Clear gating moving averages
        gating.hopsProcessed = 0;
        gating.noveltyMovingAverage = 0.0f;
        gating.semitoneFluxMovingAverage = 0.0f;
        gating.complexFluxMovingAverage = 0.0f;
        
        // Flush rolling history
        std::fill(novelty.noveltyBuffer.begin(), novelty.noveltyBuffer.end(), 0.0f);
        std::fill(novelty.amplitudeBuffer.begin(), novelty.amplitudeBuffer.end(), 0.0f);
        novelty.writePos = 0;
        novelty.hopCounter = 0;
        gating.hopsProcessed = 0;
    }

    bool hasEnoughDataForAnalysis() const {
        return novelty.hopCounter >= fft.hopSize && ringBuffer.available() >= fft.fftSize;
    }

    void readNextWindowAndComputeFFT() {
        novelty.hopCounter -= fft.hopSize;

        // Read FFT window
        ringBuffer.readLatest(fft.window.data(), fft.fftSize);

        // Calculate max amplitude for this specific window for noise gating
        fft.currentWindowMaxAmp = 0.0f;
        for (int i = 0; i < fft.fftSize; ++i) {
            float absSample = std::abs(fft.window[i]);
            if (absSample > fft.currentWindowMaxAmp) {
                fft.currentWindowMaxAmp = absSample;
            }
        }

        // FFT
        spectral.compute(fft.window.data(), fft.currentMag.data(), fft.phase.data());
    }

    float calculateNoveltyFlux() {
        // ---- Stream 1: Semitone Filter Bank Flux (Pertusa & Iñesta 2009) ----
        // Robust to vibrato and minor harmonic drift. Strong on percussion and
        // articulated harmonic instruments. Output is normalised to ~[0, 1].
        float semitoneFlux = dsp::OnsetDetection::superFlux(
            fft.currentMag.data(),
            filterBank.previousBandEnergies.data(),
            filterBank.currentBandEnergies.data(),
            filterBank.filterBank.data(),
            fft.numBins,
            filterBank.numSemitoneBands,
            1 // maxFilterWidth
        );

        // ---- Stream 2: Complex Domain (Duxbury et al. 2003) ----
        // Catches phase-only onsets that magnitude-based detectors miss
        // (bowed strings transitioning notes, sustained vocals).
        // Raw scale depends on signal amplitude - normalised below.
        float complexFlux = dsp::OnsetDetection::complexDomain(
            fft.currentMag.data(),
            fft.phase.data(),
            fft.previousMag.data(),
            fft.previousPhase.data(),
            fft.previousPreviousPhase.data(),
            fft.numBins
        );

        // ---- Adaptive gating - each stream against its own moving average ----
        float semitoneThreshold = gating.semitoneFluxMovingAverage + GATING_OFFSET;
        float gatedSemitone = (semitoneFlux > semitoneThreshold)
            ? (semitoneFlux - gating.semitoneFluxMovingAverage)
            : 0.0f;
        gating.semitoneFluxMovingAverage =
            GATING_ALPHA * semitoneFlux + (1.0f - GATING_ALPHA) * gating.semitoneFluxMovingAverage;

        float complexThreshold = gating.complexFluxMovingAverage + GATING_OFFSET;
        float gatedComplex = (complexFlux > complexThreshold)
            ? (complexFlux - gating.complexFluxMovingAverage)
            : 0.0f;
        gating.complexFluxMovingAverage =
            GATING_ALPHA * complexFlux + (1.0f - GATING_ALPHA) * gating.complexFluxMovingAverage;

        // ---- Normalise complex stream to comparable scale ----
        // Semitone flux is bounded [0, 1]; complex domain isn't. Dividing by
        // its moving average puts both streams on roughly equal footing.
        const float epsilon = 1e-6f;
        float normalisedComplex = gatedComplex / (gating.complexFluxMovingAverage + epsilon);
        // Clamp to prevent blow-ups when moving average is still warming up
        normalisedComplex = std::min(normalisedComplex, 1.0f);

        // ---- Fuse the two streams ----
        // Weighted sum - semitone flux leads (more reliable, well-bounded);
        // complex domain contributes legato/phase-onset information.
        // During warm-up, rely on semitone flux only since complex MA is unreliable.
        float combinedFlux;
        if (gating.hopsProcessed < WARMUP_HOPS) {
            combinedFlux = gatedSemitone;
        } else {
            combinedFlux = 0.6f * gatedSemitone + 0.4f * normalisedComplex;
        }

        // Logarithmic compression
        return std::log1p(FLUX_COMPRESSION_LAMBDA * combinedFlux);
    }

    void updateSilenceAndGating(float rawNovelty) {
        // If the audio window is just room noise, force the novelty peak to zero
        if (fft.currentWindowMaxAmp < config.SILENCE_THRESHOLD) {
            rawNovelty = 0.0f;
            tempo.consecutiveSilenceHops++;

            // Only bleed the engine state if the silence has lasted for > 2 seconds
            // featureRate = hops per second, so 2.0 * featureRate = 2 seconds
            if (tempo.consecutiveSilenceHops > static_cast<int>(SILENCE_TIMEOUT_SEC * fft.featureRate)) {
                tempo.currentStableBpm = tempo.currentStableBpm * 0.95; 
                tempo.highBpmConfirmationCount = 0; 
                tempo.octaveMismatchCount = 0; 
            }
        } else {
            // As soon as we hear audio, reset the silence timer
            tempo.consecutiveSilenceHops = 0;
        }

        // Apply a smoothing filter to round off jagged legato peaks
        gating.noveltyMovingAverage = 0.6f * gating.noveltyMovingAverage + 0.4f * rawNovelty;
        float noveltyVal = gating.noveltyMovingAverage;

        // Store in novelty buffer
        if (novelty.writePos < novelty.maxLength) {
            novelty.noveltyBuffer[novelty.writePos] = noveltyVal;
            novelty.amplitudeBuffer[novelty.writePos] = fft.currentWindowMaxAmp;
            novelty.writePos++;
        } else {
            std::move(novelty.noveltyBuffer.begin() + 1,
                      novelty.noveltyBuffer.end(),
                      novelty.noveltyBuffer.begin());
            novelty.noveltyBuffer.back() = noveltyVal;

            std::move(novelty.amplitudeBuffer.begin() + 1,
                      novelty.amplitudeBuffer.end(),
                      novelty.amplitudeBuffer.begin());
            novelty.amplitudeBuffer.back() = fft.currentWindowMaxAmp;
        }
    }

    void shiftBuffers() {
        // ---- Shift state for next iteration ----
        // Band energies (for semitone flux)
        std::copy(filterBank.currentBandEnergies.begin(),
                  filterBank.currentBandEnergies.end(),
                  filterBank.previousBandEnergies.begin());

        // Magnitude (for complex domain)
        std::copy(fft.currentMag.begin(),
                  fft.currentMag.end(),
                  fft.previousMag.begin());

        // Phase history (two frames back, for complex domain)
        std::copy(fft.previousPhase.begin(),
                  fft.previousPhase.end(),
                  fft.previousPreviousPhase.begin());
        std::copy(fft.phase.begin(),
                  fft.phase.end(),
                  fft.previousPhase.begin());

        gating.hopsProcessed++;
    }

    int getNoveltyLength() const {
        return std::min(novelty.writePos, novelty.maxLength);
    }

    double estimateRawTempo(float inputScale, bool useSmartMixedMode, double& outConfidence) {
        int noveltyLen = getNoveltyLength();
        
        // Dynamic Window Shift
        double currentMinBpm = config.minBpm * inputScale;
        double currentMaxBpm = config.maxBpm * inputScale;

        // Scale the center bias so the estimator doesn't drag subdivisions back to 1/4 notes
        double trackingBpm = (tempo.currentStableBpm > 0.0) ? tempo.currentStableBpm : 100.0;
        double scaledTrackingBpm = trackingBpm * inputScale;

        auto tempoResult = tempoEstimator.estimate(
            novelty.noveltyBuffer.data(), noveltyLen,
            fft.featureRate, currentMinBpm, currentMaxBpm,
            scaledTrackingBpm, outConfidence,
            useSmartMixedMode
        );
        
        return tempoResult.bpm;
    }

    void applyAlternationHeuristics(double& currentBpm) {
        int noveltyLen = getNoveltyLength();

        // --- A. Analyse (Alternation Check) ---
        if (currentBpm > FAST_TEMPO_THRESHOLD) { 
            int beatLag = static_cast<int>(60.0 / currentBpm * fft.featureRate);
            
            if (noveltyLen > beatLag * 4) {
                float evenPeaks = 0.0f;
                float oddPeaks = 0.0f;
                int searchWindow = std::max(4, static_cast<int>(beatLag * 0.15f));

                for (int i = 0; i < 8; ++i) {
                    // Look backward from the most recent write position
                    int targetPos = (noveltyLen - 1) - (i * beatLag);
                    
                    float localMax = 0.0f; 
                    for (int w = -searchWindow; w <= searchWindow; ++w) {
                        int idx = targetPos + w;
                        if (idx >= 0 && idx < noveltyLen) {
                            localMax = std::max(localMax, novelty.amplitudeBuffer[idx]);
                        }
                    }

                    if (i % 2 == 0) evenPeaks += localMax;
                    else oddPeaks += localMax;
                }

                float rawRatio = std::max(evenPeaks, oddPeaks) / (std::min(evenPeaks, oddPeaks) + 1e-6f);
                rawRatio = std::min(rawRatio, 10.0f);

                tempo.smoothedAlternationRatio = (0.1f * rawRatio) + (0.9f * tempo.smoothedAlternationRatio);
            }
        } else {
            tempo.smoothedAlternationRatio = (0.1f * 1.0f) + (0.9f * tempo.smoothedAlternationRatio);
        }

        // --- B. Veto (Hysteresis Logic) ---
        float dynamicThreshold = 1.4f; // Neutral / Startup
        if (tempo.currentStableBpm > 150.0) {
            dynamicThreshold = 1.8f; // Very loose if already at high speed
        } else if (tempo.currentStableBpm > 0.0 && tempo.currentStableBpm < 110.0) {
            dynamicThreshold = 1.15f; // Strict if locked at slow speed
        }

        if (currentBpm > FAST_TEMPO_THRESHOLD) {
            int beatLag = static_cast<int>(60.0 / currentBpm * fft.featureRate);
            
            // Veto if there is enough data
            if (noveltyLen > beatLag * 4) {
                if (tempo.smoothedAlternationRatio > dynamicThreshold) {
                    // Strum-Tap
                    currentBpm *= 0.5;
                    tempo.highBpmConfirmationCount = std::max(0, tempo.highBpmConfirmationCount - 2); 
                } else {
                    // Fast Tempo, Increment streak
                    tempo.highBpmConfirmationCount = std::min(20, tempo.highBpmConfirmationCount + 1);
                }
            } else {
                tempo.highBpmConfirmationCount = 12; 
            }
        } else {
            tempo.highBpmConfirmationCount = std::max(0, tempo.highBpmConfirmationCount - 1);
        }
    }

    void applyHysteresisAndCommit(double& currentBpm) {
        // --- C. COMMIT (Hysteresis/Escape Hatch) ---
        if (tempo.currentStableBpm == 0.0) {
            tempo.currentStableBpm = currentBpm;
        } else {
            bool isOctaveError = false;
            
            // Defending against double-time and half-time jumps
            if (std::abs(currentBpm - (tempo.currentStableBpm * 2.0)) < config.doubleTimeJumpThreshold) {
                tempo.octaveMismatchCount++;
                isOctaveError = true;
            } else if (std::abs(currentBpm - (tempo.currentStableBpm * 0.5)) < config.halfTimeJumpThreshold) {
                tempo.octaveMismatchCount++;
                isOctaveError = true;
            }

            if (isOctaveError) {
                // If it insists on the octave for 30 frames, break the lock
                if (tempo.octaveMismatchCount > 30) {
                    tempo.currentStableBpm = currentBpm; 
                    tempo.octaveMismatchCount = 0;
                } else {
                    // Clamp it to the stable reading
                    currentBpm = tempo.currentStableBpm; 
                }
            } else {
                tempo.octaveMismatchCount = 0; 
                // Smoothing and Hard Resets
                if (std::abs(currentBpm - tempo.currentStableBpm) > 20.0) {
                    // Hard reset if it's a massive shift
                    tempo.currentStableBpm = currentBpm;
                } else {
                    // Stiffen the glide so FFT window drift can't drag it down
                    tempo.currentStableBpm = 0.95 * tempo.currentStableBpm + 0.05 * currentBpm;
                    // Assign the smoothed value back
                    currentBpm = tempo.currentStableBpm;
                }
            }
        }
    }
};

Engine::Engine(const Config& config)
    : pImpl_(std::make_unique<Impl>(config))
{
}

Engine::~Engine() = default;

void Engine::processBlock(const float* samples, int numSamples) {
    auto start = std::chrono::high_resolution_clock::now();
    auto& d = *pImpl_;

    d.ringBuffer.write(samples, numSamples);
    d.novelty.hopCounter += numSamples;

    while (d.hasEnoughDataForAnalysis()) {
        d.readNextWindowAndComputeFFT();
        
        float rawNovelty = d.calculateNoveltyFlux();
        d.updateSilenceAndGating(rawNovelty);
        d.shiftBuffers();
    }

    // Re-estimate tempo
    int noveltyLen = d.getNoveltyLength();

    if (noveltyLen > 50) {
        
        // Mathematical scale based on user input
        float inputScale = 1.0f;
        bool useSmartMixedMode = false;
        
        if (d.config.targetResolution == NoteResolution::SixteenthNote) inputScale = 4.0f;
        else if (d.config.targetResolution == NoteResolution::EighthNoteTriplet) inputScale = 3.0f;
        else if (d.config.targetResolution == NoteResolution::EighthNote) inputScale = 2.0f;
        else if (d.config.targetResolution == NoteResolution::HalfNote) inputScale = 0.5f;
        else if (d.config.targetResolution == NoteResolution::SmartMixed) { inputScale = 1.0f; useSmartMixedMode = true; }

        double rawBpm = d.estimateRawTempo(inputScale, useSmartMixedMode, latestResult_.confidence);

        if (rawBpm > 0) {
        /* DEBUG LOGGING
            std::cout << "[DSP] Raw Detected: " << rawBpm 
                      << " | Stable: " << d.tempo.currentStableBpm 
                      << " | Ratio: " << d.tempo.smoothedAlternationRatio << "\n";
        */
            
            // Immediately scale the detected pulse back to Master 1/4 Note Tempo
            rawBpm /= inputScale;

            d.applyAlternationHeuristics(rawBpm);
            d.applyHysteresisAndCommit(rawBpm);

            double outputBpm = rawBpm;
            bool alreadyLockedFast = (d.tempo.currentStableBpm > d.Impl::FAST_TEMPO_THRESHOLD);
            
            if (!alreadyLockedFast && rawBpm > d.Impl::FAST_TEMPO_THRESHOLD && d.tempo.highBpmConfirmationCount < 12) {
                outputBpm *= 0.5;
            }

            // Set the beat tracker lag using the resolution BPM
            double trackingBpmForBeatTracker = outputBpm * inputScale;
            int beatRefLag = static_cast<int>(60.0 / trackingBpmForBeatTracker * d.fft.featureRate);
            
            auto dpResult = d.beatTracker.track(d.novelty.noveltyBuffer.data(), noveltyLen, beatRefLag);

            Result result;
            
            // Assign final smoothed BPM
            result.bpm = outputBpm; 
            result.confidence = 0.4 * dpResult.stability
                              + 0.4 * dpResult.alignment
                              + 0.2 * dpResult.coverage;
            result.hasValidEstimate = (result.confidence > 0.2);

            auto end = std::chrono::high_resolution_clock::now();
            result.processingTimeMs =
                std::chrono::duration<double, std::milli>(end - start).count();

            latestResult_ = result;
        }
    }
}

Result Engine::getLatestResult() const {
    return latestResult_;
}

double Engine::getVisualDeadzone() const {
    return pImpl_->config.VISUAL_DEADZONE;
}

void Engine::setTargetResolution(NoteResolution resolution) {
    if (pImpl_->config.targetResolution != resolution) {
        pImpl_->config.targetResolution = resolution;
        
        reset(); // Flush the engine state to prevent weirdness when switching resolutions
    }
}

void Engine::reset() {
    pImpl_->flushTrackingState();
    latestResult_ = Result{};
}

} // namespace livebpm