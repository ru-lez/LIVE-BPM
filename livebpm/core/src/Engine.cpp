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
    Config config;
    int fftSize;
    int hopSize;
    int numBins;
    double featureRate;

    dsp::RingBuffer<float> ringBuffer;
    dsp::SpectralAnalysis spectral;
    dsp::TempoEstimation tempo;
    dsp::BeatTrackingDP beatTracker;

    // FFT working buffers
    std::vector<float> fftWindow;
    std::vector<float> currentMag;
    std::vector<float> previousMag;          // for complex domain
    std::vector<float> phase;
    std::vector<float> previousPhase;        // for complex domain
    std::vector<float> previousPreviousPhase; // for complex domain

    // Semitone filter bank state
    std::vector<float> filterBank;
    std::vector<float> currentBandEnergies;
    std::vector<float> previousBandEnergies;
    int numSemitoneBands = 0;

    // Adaptive gating - independent moving averages per stream
    float semitoneFluxMovingAverage = 0.0f;
    float complexFluxMovingAverage = 0.0f;
    float noveltyMovingAverage = 0.0f;
    float smoothedAlternationRatio = 1.0f;

    // Warm-up counter so we don't fuse with garbage moving averages
    int hopsProcessed = 0;
    static constexpr int warmupHops = 50;

    // Novelty buffer for tempo estimation
    std::vector<float> noveltyBuffer;
    std::vector<float> amplitudeBuffer;
    int hopCounter = 0;
    int noveltyWritePos = 0;
    int maxNoveltyLength;

    // Tempo hysteresis: hold the last stable BPM to prevent erratic jumps when the signal is weak or ambiguous
    double currentStableBpm = 0.0;
    int octaveMismatchCount = 0;
    int highBpmConfirmationCount = 0;
    int consecutiveSilenceHops = 0;

    Impl(const Config& cfg)
        : config(cfg),
          fftSize(1 << cfg.fftOrder),
          hopSize(fftSize / 4),
          numBins(fftSize / 2 + 1),
          featureRate(cfg.sampleRate / hopSize),
          ringBuffer(fftSize * 2),
          spectral(fftSize),
          tempo(static_cast<int>(cfg.analysisWindowSeconds * featureRate)),
          fftWindow(fftSize),
          currentMag(numBins, 0.0f),
          previousMag(numBins, 0.0f),
          phase(numBins, 0.0f),
          previousPhase(numBins, 0.0f),
          previousPreviousPhase(numBins, 0.0f)
    {
        // Build filter bank once at startup
        filterBank = dsp::OnsetDetection::buildSemitoneFilterBank(
            numBins,
            static_cast<float>(cfg.sampleRate),
            fftSize,
            55.0f,    // A1 lower bound
            8000.0f   // ~B8 upper bound
        );

        numSemitoneBands = static_cast<int>(filterBank.size()) / numBins;
        currentBandEnergies.resize(numSemitoneBands, 0.0f);
        previousBandEnergies.resize(numSemitoneBands, 0.0f);

        maxNoveltyLength = static_cast<int>(cfg.analysisWindowSeconds * featureRate);
        noveltyBuffer.resize(maxNoveltyLength, 0.0f);
        amplitudeBuffer.resize(maxNoveltyLength, 0.0f);
        std::cout << "[Engine] Initialised semitone filter bank: "
                  << numSemitoneBands << " bands\n";
        std::cout << "[Engine] Onset detection: semitone flux + complex domain (fused)\n";
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
    d.hopCounter += numSamples;

    while (d.hopCounter >= d.hopSize && d.ringBuffer.available() >= d.fftSize) {
        d.hopCounter -= d.hopSize;

        // Read FFT window
        d.ringBuffer.readLatest(d.fftWindow.data(), d.fftSize);

        // Calculate max amplitude for this specific window for noise gating
        float windowMaxAmp = 0.0f;
        for (int i = 0; i < d.fftSize; ++i) {
            float absSample = std::abs(d.fftWindow[i]);
            if (absSample > windowMaxAmp) {
                windowMaxAmp = absSample;
            }
        }

        // FFT
        d.spectral.compute(d.fftWindow.data(),
                           d.currentMag.data(),
                           d.phase.data());

        // ---- Stream 1: Semitone Filter Bank Flux (Pertusa & Iñesta 2009) ----
        // Robust to vibrato and minor harmonic drift. Strong on percussion and
        // articulated harmonic instruments. Output is normalised to ~[0, 1].
        float semitoneFlux = dsp::OnsetDetection::superFlux(
            d.currentMag.data(),
            d.previousBandEnergies.data(),
            d.currentBandEnergies.data(),
            d.filterBank.data(),
            d.numBins,
            d.numSemitoneBands,
            1 // maxFilterWidth
        );

        // ---- Stream 2: Complex Domain (Duxbury et al. 2003) ----
        // Catches phase-only onsets that magnitude-based detectors miss
        // (bowed strings transitioning notes, sustained vocals).
        // Raw scale depends on signal amplitude - normalised below.
        float complexFlux = dsp::OnsetDetection::complexDomain(
            d.currentMag.data(),
            d.phase.data(),
            d.previousMag.data(),
            d.previousPhase.data(),
            d.previousPreviousPhase.data(),
            d.numBins
        );

        // ---- Adaptive gating - each stream against its own moving average ----
        const float gateOffset = 0.015f;
        const float alpha = 0.1f;

        float semitoneThreshold = d.semitoneFluxMovingAverage + gateOffset;
        float gatedSemitone = (semitoneFlux > semitoneThreshold)
            ? (semitoneFlux - d.semitoneFluxMovingAverage)
            : 0.0f;
        d.semitoneFluxMovingAverage =
            alpha * semitoneFlux + (1.0f - alpha) * d.semitoneFluxMovingAverage;

        float complexThreshold = d.complexFluxMovingAverage + gateOffset;
        float gatedComplex = (complexFlux > complexThreshold)
            ? (complexFlux - d.complexFluxMovingAverage)
            : 0.0f;
        d.complexFluxMovingAverage =
            alpha * complexFlux + (1.0f - alpha) * d.complexFluxMovingAverage;

        // ---- Normalise complex stream to comparable scale ----
        // Semitone flux is bounded [0, 1]; complex domain isn't. Dividing by
        // its moving average puts both streams on roughly equal footing.
        const float epsilon = 1e-6f;
        float normalisedComplex = gatedComplex / (d.complexFluxMovingAverage + epsilon);
        // Clamp to prevent blow-ups when moving average is still warming up
        normalisedComplex = std::min(normalisedComplex, 1.0f);

        // ---- Fuse the two streams ----
        // Weighted sum - semitone flux leads (more reliable, well-bounded);
        // complex domain contributes legato/phase-onset information.
        // During warm-up, rely on semitone flux only since complex MA is unreliable.
        float combinedFlux;
        if (d.hopsProcessed < d.warmupHops) {
            combinedFlux = gatedSemitone;
        } else {
            combinedFlux = 0.6f * gatedSemitone + 0.4f * normalisedComplex;
        }

        // Logarithmic compression
        const float lambda = 50.0f;
        float rawNovelty = std::log1p(lambda * combinedFlux);

        // If the audio window is just room noise, force the novelty peak to zero
        if (windowMaxAmp < d.config.SILENCE_THRESHOLD) {
            rawNovelty = 0.0f;
            d.consecutiveSilenceHops++;

            // Only bleed the engine state if the silence has lasted for > 2 seconds
            // featureRate = hops per second, so 2.0 * featureRate = 2 seconds
            if (d.consecutiveSilenceHops > static_cast<int>(2.0 * d.featureRate)) {
                d.currentStableBpm = d.currentStableBpm * 0.95; 
                d.highBpmConfirmationCount = 0; 
                d.octaveMismatchCount = 0; 
            }
        } else {
            // As soon as we hear audio, reset the silence timer
            d.consecutiveSilenceHops = 0;
        }

        // Apply a smoothing filter to round off jagged legato peaks
        d.noveltyMovingAverage = 0.6f * d.noveltyMovingAverage + 0.4f * rawNovelty;
        float novelty = d.noveltyMovingAverage;

        /*
        #ifdef LIVEBPM_DEBUG
                std::cout << "Semi: " << std::fixed << std::setprecision(4) << gatedSemitone
                          << " | Cplx: " << normalisedComplex
                          << " | Combined: " << combinedFlux
                          << " | Novelty: " << novelty << "\n";
        #endif
        */

        // Store in novelty buffer
        if (d.noveltyWritePos < d.maxNoveltyLength) {
            d.noveltyBuffer[d.noveltyWritePos] = novelty;
            d.amplitudeBuffer[d.noveltyWritePos] = windowMaxAmp;
            d.noveltyWritePos++;
        } else {
            std::move(d.noveltyBuffer.begin() + 1,
                      d.noveltyBuffer.end(),
                      d.noveltyBuffer.begin());
            d.noveltyBuffer.back() = novelty;
        }

        // ---- Shift state for next iteration ----
        // Band energies (for semitone flux)
        std::copy(d.currentBandEnergies.begin(),
                  d.currentBandEnergies.end(),
                  d.previousBandEnergies.begin());

        // Magnitude (for complex domain)
        std::copy(d.currentMag.begin(),
                  d.currentMag.end(),
                  d.previousMag.begin());

        // Phase history (two frames back, for complex domain)
        std::copy(d.previousPhase.begin(),
                  d.previousPhase.end(),
                  d.previousPreviousPhase.begin());
        std::copy(d.phase.begin(),
                  d.phase.end(),
                  d.previousPhase.begin());

        d.hopsProcessed++;
    }

    // Re-estimate tempo
    int noveltyLen = std::min(d.noveltyWritePos, d.maxNoveltyLength);
    if (noveltyLen > 50) {
        
        // Mathematical scale based on user input
        float inputScale = 1.0f;
        if (d.config.targetResolution == NoteResolution::SixteenthNote) inputScale = 4.0f;
        else if (d.config.targetResolution == NoteResolution::EighthNoteTriplet) inputScale = 3.0f;
        else if (d.config.targetResolution == NoteResolution::EighthNote) inputScale = 2.0f;
        else if (d.config.targetResolution == NoteResolution::HalfNote) inputScale = 0.5f;

        // Dynamic Window Shift (Allowing engine to search up to 360+ BPM for 16ths)
        double currentMinBpm = std::min(d.config.minBpm, (double)(d.config.minBpm * inputScale));
        double currentMaxBpm = std::max(d.config.maxBpm, (double)(d.config.maxBpm * inputScale));

        auto tempoResult = d.tempo.estimate(
            d.noveltyBuffer.data(), noveltyLen,
            d.featureRate, currentMinBpm, currentMaxBpm,
            d.currentStableBpm, latestResult_.confidence
        );

        if (tempoResult.bpm > 0) {
            
            std::cout << "[DSP] Raw Detected: " << tempoResult.bpm 
                      << " | Stable: " << d.currentStableBpm 
                      << " | Ratio: " << d.smoothedAlternationRatio << "\n";
            
            
            // Immediately scale the detected pulse back to Master 1/4 Note Tempo
            tempoResult.bpm /= inputScale;

            // --- A. ANALYZE (Alternation Check) ---
            if (tempoResult.bpm > 140.0) { 
                int beatLag = static_cast<int>(60.0 / tempoResult.bpm * d.featureRate);
                
                if (noveltyLen > beatLag * 4) {
                    float evenPeaks = 0.0f;
                    float oddPeaks = 0.0f;
                    int searchWindow = 4;

                    for (int i = 0; i < 4; ++i) {
                        // Look backward from the most recent write position
                        int targetPos = (noveltyLen - 1) - (i * beatLag);
                        
                        float localMax = 0.0f; 
                        for (int w = -searchWindow; w <= searchWindow; ++w) {
                            int idx = targetPos + w;
                            if (idx >= 0 && idx < noveltyLen) {
                                localMax = std::max(localMax, d.amplitudeBuffer[idx]);
                            }
                        }

                        if (i % 2 == 0) evenPeaks += localMax;
                        else oddPeaks += localMax;
                    }

                    float rawRatio = std::max(evenPeaks, oddPeaks) / (std::min(evenPeaks, oddPeaks) + 1e-6f);
                    rawRatio = std::min(rawRatio, 10.0f);

                    d.smoothedAlternationRatio = (0.3f * rawRatio) + (0.7f * d.smoothedAlternationRatio);
                }
            } else {
                d.smoothedAlternationRatio = (0.1f * 1.0f) + (0.9f * d.smoothedAlternationRatio);
            }

            // --- B. Veto (Hysteresis Logic) ---
            float dynamicThreshold = 2.5f; // Neutral / Startup
            if (d.currentStableBpm > 150.0) {
                dynamicThreshold = 3.5f; // Very loose if already at high speed
            } else if (d.currentStableBpm > 0.0 && d.currentStableBpm < 110.0) {
                dynamicThreshold = 2.0f; // Strict if locked at slow speed
            }

            if (tempoResult.bpm > 140.0) {
                int beatLag = static_cast<int>(60.0 / tempoResult.bpm * d.featureRate);
                
                // Veto if there is enough data
                if (noveltyLen > beatLag * 4) {
                    if (d.smoothedAlternationRatio > dynamicThreshold) {
                        // Strum-Tap
                        tempoResult.bpm *= 0.5;
                        d.highBpmConfirmationCount = std::max(0, d.highBpmConfirmationCount - 2); 
                    } else {
                        // Fast Tempo, Increment streak
                        d.highBpmConfirmationCount = std::min(20, d.highBpmConfirmationCount + 1);
                        if (d.highBpmConfirmationCount < 12) {
                            tempoResult.bpm *= 0.5;
                        }
                    }
                } else {
                    d.highBpmConfirmationCount = 12; 
                }
            } else {
                d.highBpmConfirmationCount = 0;
            }

            // --- C. COMMIT (Hysteresis/Escape Hatch) ---
            if (d.currentStableBpm == 0.0) {
                d.currentStableBpm = tempoResult.bpm;
            } else {
                bool isOctaveError = false;
                
                // Defending against double-time and half-time jumps
                if (std::abs(tempoResult.bpm - (d.currentStableBpm * 2.0)) < d.config.doubleTimeJumpThreshold) {
                    d.octaveMismatchCount++;
                    isOctaveError = true;
                } else if (std::abs(tempoResult.bpm - (d.currentStableBpm * 0.5)) < d.config.halfTimeJumpThreshold) {
                    d.octaveMismatchCount++;
                    isOctaveError = true;
                }

                if (isOctaveError) {
                    // If it insists on the octave for 3 frames, break the lock
                    if (d.octaveMismatchCount > 3) {
                        d.currentStableBpm = tempoResult.bpm; 
                        d.octaveMismatchCount = 0;
                    } else {
                        // Clamp it to the stable reading
                        tempoResult.bpm = d.currentStableBpm; 
                    }
                } else {
                    d.octaveMismatchCount = 0; 
                    // Smoothing and Hard Resets
                    if (std::abs(tempoResult.bpm - d.currentStableBpm) > 20.0) {
                        // Hard reset if it's a massive shift
                        d.currentStableBpm = tempoResult.bpm;
                    } else {
                        // Otherwise glide smoothly
                        d.currentStableBpm = 0.85 * d.currentStableBpm + 0.15 * tempoResult.bpm;
                    }
                }
            }

            // Set the beat tracker lag using the corrected BPM
            int beatRefLag = static_cast<int>(60.0 / tempoResult.bpm * d.featureRate);
            auto dpResult = d.beatTracker.track(d.noveltyBuffer.data(), noveltyLen, beatRefLag);

            Result result;
            
            // Assign final smoothed BPM
            result.bpm = tempoResult.bpm; 
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

void Engine::setTargetResolution(NoteResolution resolution) {
    pImpl_->config.targetResolution = resolution;
}

double Engine::getVisualDeadzone() const {
    return pImpl_->config.VISUAL_DEADZONE;
}

void Engine::reset() {
    pImpl_ = std::make_unique<Impl>(pImpl_->config);
    latestResult_ = Result{};
}

} // namespace livebpm