#include "livebpm/Engine.h"
#include "livebpm/dsp/SpectralAnalysis.h"
#include "livebpm/dsp/OnsetDetection.h"
#include "livebpm/dsp/PitchTracking.h"
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
    float anchorPitch = 0.0f;
    int pitchHoldoff = 0;
    int unvoicedFrames = 0;
    std::vector<float> recentPitches{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> fluxDelay{0.0f, 0.0f, 0.0f}; 
    std::vector<float> cdDelay{0.0f, 0.0f, 0.0f};

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

        // Calculate Complex Domain
        float cd = dsp::OnsetDetection::complexDomain(
            d.currentMag.data(), d.phase.data(), 
            d.previousMag.data(), d.previousPhase.data(), 
            d.previousPreviousPhase.data(), d.numBins
        );
        
        // Calculate average magnitude (energy) of the current frame
        float frameEnergy = 0.0f;
        for (int k = 0; k < d.numBins; ++k) {
            frameEnergy += d.currentMag[k];
        }
        frameEnergy /= static_cast<float>(d.numBins);

        // Get raw pitch from MPM
        float rawPitch = 0.0f;

        // Only evaluate pitch if the frame has enough energy to be a real instrument
        if (frameEnergy > 0.005f) { 
            rawPitch = dsp::PitchTracking::estimatePitchMPM(
                d.fftWindow.data(), d.fftSize, static_cast<float>(d.config.sampleRate)
            );
        }
        
        // 2. Apply 5-point Median Filter to eliminate 1-frame octave glitches
        d.recentPitches.erase(d.recentPitches.begin()); 
        d.recentPitches.push_back(rawPitch);
        
        std::vector<float> sortedPitches = d.recentPitches;
        std::sort(sortedPitches.begin(), sortedPitches.end());
        float currentPitch = sortedPitches[2];
        
        float pitchFlux = 0.0f;
        
        // Decrement holdoff timer
        if (d.pitchHoldoff > 0) {
            d.pitchHoldoff--;
        }
        
        // Check filtered pitch
        if (currentPitch > 0.0f) {
            d.unvoicedFrames = 0; 

            if (d.anchorPitch == 0.0f) {
                d.anchorPitch = currentPitch; 
            } else {
                float currentMidi = 69.0f + 12.0f * std::log2(currentPitch / 440.0f);
                float anchorMidi = 69.0f + 12.0f * std::log2(d.anchorPitch / 440.0f);
                float midiDiff = std::abs(currentMidi - anchorMidi);
                
                if (midiDiff > 1.25f) { 
                    if (d.pitchHoldoff == 0) {
                        pitchFlux = 1.0f; 
                        d.pitchHoldoff = 12; 
                    }
                    d.anchorPitch = currentPitch; 
                } else {
                    d.anchorPitch = (0.95f * d.anchorPitch) + (0.05f * currentPitch);
                }
            }
        } else {
            d.unvoicedFrames++;
            if (d.unvoicedFrames > 5) {
                d.anchorPitch = 0.0f;
            }
        }

        d.fluxDelay.push_back(flux);
        d.cdDelay.push_back(cd);
        float alignedFlux = d.fluxDelay.front();
        float alignedCd = d.cdDelay.front();
        d.fluxDelay.erase(d.fluxDelay.begin());
        d.cdDelay.erase(d.cdDelay.begin());


        // Combine Complex Domain and Spectral Flux
        // Logarithmic combination to balance the scales and enhance peaks
        // 1. Spectral Flux
        // 2. Complex Domain
        // 3. Pitch Flux

        // Thresholds to suppress low-level noise while preserving real onsets
        float fluxFloor = 0.02f;
        float cdFloor = 0.8f;
        float lambda = 10.0f;

        alignedFlux = std::max(0.0f, alignedFlux - fluxFloor);
        alignedCd = std::max(0.0f, alignedCd - cdFloor);
        float weightedPitchFlux = 1.5f * pitchFlux;

        float combinedNovelty = std::log1p(lambda * (
                                                    alignedFlux
                                                  + alignedCd
                                                  + weightedPitchFlux
                                          ));
        
        // DEBUG: raw novelty values in console
        std::cout << "Spectral Flux: " << alignedFlux << " | CD: " << alignedCd << " | Pitch Flux: " << weightedPitchFlux << " | Combined: " << combinedNovelty << "\n";
        // std::cout << "Current Pitch: " << currentPitch << " Hz | Anchor Pitch: " << d.anchorPitch << " Hz | Unvoiced Frames: " << d.unvoicedFrames << " | Holdoff: " << d.pitchHoldoff << "\n";
        // std::cout << "Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() << " ms  \n";

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