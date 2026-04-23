#include "livebpm/Engine.h"
#include "livebpm/dsp/SpectralAnalysis.h"
#include "livebpm/dsp/OnsetDetection.h"
#include "livebpm/dsp/TempoEstimation.h"
#include "livebpm/dsp/BeatTrackingDP.h"
#include "livebpm/dsp/RingBuffer.h"
#include <iostream>

#include <chrono>
#include <algorithm>
#include <vector>

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

    // Pre-allocated buffers
    std::vector<float> fftWindow;
    std::vector<float> currentMag;
    std::vector<float> previousMag;
    std::vector<float> phase;
    std::vector<float> previousPhase;
    std::vector<float> previousPreviousPhase;
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
        maxNoveltyLength = static_cast<int>(cfg.analysisWindowSeconds * featureRate);
        noveltyBuffer.resize(maxNoveltyLength, 0.0f);
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

    // Process each time we accumulate a complete hop
    while (d.hopCounter >= d.hopSize && d.ringBuffer.available() >= d.fftSize) {
        d.hopCounter -= d.hopSize;

        // Read FFT window from ring buffer
        d.ringBuffer.readLatest(d.fftWindow.data(), d.fftSize);

        // FFT
        d.spectral.compute(d.fftWindow.data(), d.currentMag.data(), d.phase.data());

        // Calculate Spectral Flux
        float flux = dsp::OnsetDetection::spectralFlux(
            d.currentMag.data(), d.previousMag.data(), d.numBins);

        // Complex Domain Onset Detection
        float cd = dsp::OnsetDetection::complexDomain(
            d.currentMag.data(), d.phase.data(), 
            d.previousMag.data(), d.previousPhase.data(), 
            d.previousPreviousPhase.data(), d.numBins
        );

        // Combine Complex Domain and Spectral Flux
        // Use a logarithmic combination to balance the scales and enhance peaks
        float lambda = 10.0f;
        float combinedNovelty = std::log1p(lambda * (flux + cd));
        
        // DEBUG: raw novelty values in console
        // std::cout << "Flux: " << flux << " | CD: " << cd << " | Combined: " << combinedNovelty << "\n";

        // Store in novelty buffer (circular)
        if (d.noveltyWritePos < d.maxNoveltyLength) {
            d.noveltyBuffer[d.noveltyWritePos] = combinedNovelty;
            d.noveltyWritePos++;
        } else {
            // Shift left
            std::move(d.noveltyBuffer.begin() + 1, d.noveltyBuffer.end(), d.noveltyBuffer.begin());
            d.noveltyBuffer.back() = combinedNovelty;
        }

        // Shift phase and magnitude buffers for next iteration
        std::copy(d.previousPhase.begin(), d.previousPhase.end(), d.previousPreviousPhase.begin());
        std::copy(d.phase.begin(), d.phase.end(), d.previousPhase.begin());
        std::copy(d.currentMag.begin(), d.currentMag.end(), d.previousMag.begin());
    }

    // Re-estimate tempo periodically (not on every hop)
    int noveltyLen = std::min(d.noveltyWritePos, d.maxNoveltyLength);
    if (noveltyLen > 50) { // Minimum ~0.5 seconds of data
        auto tempoResult = d.tempo.estimate(
            d.noveltyBuffer.data(), noveltyLen, d.featureRate, d.config.minBpm, d.config.maxBpm
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
            result.processingTimeMs = std::chrono::duration<double, std::milli>(end - start).count();

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