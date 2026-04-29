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
    std::vector<float> previousMag;       // for spectral flux
    std::vector<float> phase;

    // Semitone filter bank state
    std::vector<float> filterBank;            // precomputed triangular filters
    std::vector<float> currentBandEnergies;
    std::vector<float> previousBandEnergies;
    int numSemitoneBands = 0;

    // Adaptive gating moving averages
    float fluxMovingAverage         = 0.0f;
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
          previousMag(numBins, 0.0f),
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

        // ============================================================
        // ONSET DETECTION — TWO COMPLEMENTARY METHODS
        // ============================================================

        // ---- Spectral Flux (strong on broadband percussive onsets) ----
        float spectralFlux = dsp::OnsetDetection::spectralFlux(
            d.currentMag.data(), d.previousMag.data(), d.numBins
        );

        // ---- Semitone Filter Bank (strong on pitched/legato, vibrato-robust) ----
        float semitoneFlux = dsp::OnsetDetection::semitoneFlux(
            d.currentMag.data(),
            d.previousBandEnergies.data(),
            d.currentBandEnergies.data(),
            d.filterBank.data(),
            d.numBins,
            d.numSemitoneBands
        );

        // ============================================================
        // ADAPTIVE NOISE GATING
        // ============================================================

        const float fluxGateOffset     = 0.015f;  // higher — flux noise floor is wider
        const float semitoneGateOffset = 0.005f;  // lower — normalised signal
        const float alpha              = 0.05f;   // moving-average adaptation speed

        float fluxThreshold     = d.fluxMovingAverage         + fluxGateOffset;
        float semitoneThreshold = d.semitoneFluxMovingAverage + semitoneGateOffset;

        float gatedFlux = (spectralFlux > fluxThreshold)
            ? (spectralFlux - d.fluxMovingAverage) : 0.0f;
        float gatedSemitone = (semitoneFlux > semitoneThreshold)
            ? (semitoneFlux - d.semitoneFluxMovingAverage) : 0.0f;

        // Update moving averages (exponential smoothing)
        d.fluxMovingAverage =
            alpha * spectralFlux + (1.0f - alpha) * d.fluxMovingAverage;
        d.semitoneFluxMovingAverage =
            alpha * semitoneFlux + (1.0f - alpha) * d.semitoneFluxMovingAverage;

        // ============================================================
        // COMBINE NOVELTY SIGNALS
        // ============================================================

        const float lambda          = 50.0f;
        const float spectralWeight  = 1.0f;
        const float semitoneWeight  = 3.0f;  // start point — tune for your material

        float novelty = std::log1p(lambda * (
                          spectralWeight * gatedFlux
                        + semitoneWeight * gatedSemitone
                      ));
        
        /*
        #ifdef LIVEBPM_DEBUG
                std::cout << std::fixed << std::setprecision(4)
                        << "Flux: "      << gatedFlux
                        << " | Semi: "   << gatedSemitone
                        << " | Novelty: "<< novelty << "\n";
        #endif
        */

        // ============================================================
        // STORE NOVELTY + SHIFT STATE FOR NEXT ITERATION
        // ============================================================

        if (d.noveltyWritePos < d.maxNoveltyLength) {
            d.noveltyBuffer[d.noveltyWritePos] = novelty;
            d.noveltyWritePos++;
        } else {
            std::move(d.noveltyBuffer.begin() + 1,
                      d.noveltyBuffer.end(),
                      d.noveltyBuffer.begin());
            d.noveltyBuffer.back() = novelty;
        }

        // Shift state for next frame
        std::copy(d.currentBandEnergies.begin(),
                  d.currentBandEnergies.end(),
                  d.previousBandEnergies.begin());
        std::copy(d.currentMag.begin(),
                  d.currentMag.end(),
                  d.previousMag.begin());
    }

    // ============================================================
    // PERIODIC TEMPO RE-ESTIMATION
    // ============================================================

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