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

    // Warm-up counter so we don't fuse with garbage moving averages
    int hopsProcessed = 0;
    static constexpr int warmupHops = 50;

    // Novelty buffer for tempo estimation
    std::vector<float> noveltyBuffer;
    int hopCounter = 0;
    int noveltyWritePos = 0;
    int maxNoveltyLength;

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

        // FFT
        d.spectral.compute(d.fftWindow.data(),
                           d.currentMag.data(),
                           d.phase.data());

        // ---- Stream 1: Semitone Filter Bank Flux (Pertusa & Iñesta 2009) ----
        // Robust to vibrato and minor harmonic drift. Strong on percussion and
        // articulated harmonic instruments. Output is normalised to ~[0, 1].
        float semitoneFlux = dsp::OnsetDetection::semitoneFlux(
            d.currentMag.data(),
            d.previousBandEnergies.data(),
            d.currentBandEnergies.data(),
            d.filterBank.data(),
            d.numBins,
            d.numSemitoneBands
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
        const float gateOffset = 0.005f;
        const float alpha = 0.05f;

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
        float novelty = std::log1p(lambda * combinedFlux);

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
        auto tempoResult = d.tempo.estimate(
            d.noveltyBuffer.data(), noveltyLen,
            d.featureRate, d.config.minBpm, d.config.maxBpm
        );

        if (tempoResult.bpm > 0) {
            int beatRefLag = static_cast<int>(60.0 / tempoResult.bpm * d.featureRate);
            auto dpResult = d.beatTracker.track(d.noveltyBuffer.data(), noveltyLen, beatRefLag);

            Result result;
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

void Engine::reset() {
    pImpl_ = std::make_unique<Impl>(pImpl_->config);
    latestResult_ = Result{};
}

} // namespace livebpm