#pragma once

namespace livebpm {
namespace dsp {

class OnsetDetection {
public:
    // Spectral Flux (half-wave rectified)
    static float spectralFlux(const float* currentMag,
                              const float* previousMag,
                              int numBins);

    // Complex Domain onset detection
    static float complexDomain(const float* currentMag, const float* currentPhase,
                               const float* prevMag, const float* prevPhase,
                               const float* prevPrevPhase, int numBins);

    // Log-Frequency Spectral Flux
    static float logFrequencyFlux(const float* currentMag, 
                                  const float* previousMag,
                                  int numBins, float sampleRate, int fftSize);
};



}} // namespace livebpm::dsp