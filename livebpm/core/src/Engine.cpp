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
    std::vector<float> phase;

    // Semitone filter bank state
    std::vector<float> filterBank;            // precomputed triangular filters
    std::vector<float> currentBandEnergies;
    std::vector<float> previousBandEnergies;
    int numSemitoneBands = 0;

    // Adaptive gating for semitone flux
    float semitoneFluxMovingAverage = 0.0f;

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
          phase(numBins, 0.0f)
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

        // ---- Semitone Filter Bank Flux (Pertusa & Iñesta 2009) ----
        float semitoneFlux = dsp::OnsetDetection::semitoneFlux(
            d.currentMag.data(),
            d.previousBandEnergies.data(),
            d.currentBandEnergies.data(),
            d.filterBank.data(),
            d.numBins,
            d.numSemitoneBands
        );

        // Adaptive noise gate
        const float gateOffset = 0.005f;
        float threshold = d.semitoneFluxMovingAverage + gateOffset;

        float gatedFlux = (semitoneFlux > threshold)
            ? (semitoneFlux - d.semitoneFluxMovingAverage)
            : 0.0f;

        // Update moving average
        const float alpha = 0.05f;
        d.semitoneFluxMovingAverage =
            alpha * semitoneFlux + (1.0f - alpha) * d.semitoneFluxMovingAverage;

        // Logarithmic compression
        const float lambda = 50.0f;  // higher because semitoneFlux is normalised [0,1]
        float novelty = std::log1p(lambda * gatedFlux);
        /*
        #ifdef LIVEBPM_DEBUG
                std::cout << "SemitoneFlux: " << std::fixed << std::setprecision(4)
                        << semitoneFlux
                        << " | Avg: " << d.semitoneFluxMovingAverage
                        << " | Gated: " << gatedFlux
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

        // Shift band energies for next iteration
        std::copy(d.currentBandEnergies.begin(),
                  d.currentBandEnergies.end(),
                  d.previousBandEnergies.begin());
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