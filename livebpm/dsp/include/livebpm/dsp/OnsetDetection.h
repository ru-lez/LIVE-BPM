#include <vector>
#pragma once

namespace livebpm {
namespace dsp {

class OnsetDetection {
public:
    // Complex Domain onset detection
    static float complexDomain(const float* currentMag, const float* currentPhase,
                               const float* prevMag, const float* prevPhase,
                               const float* prevPrevPhase, int numBins);


    // Pertusa & Iñesta (2009): One-semitone filter bank onset detection
    // Groups FFT bins into musically meaningful pitch bands.
    // Robust to vibrato and frequency wobble within a single semitone.
    //
    //   currentMag           = magnitude spectrum from current FFT
    //   previousBandEnergies = band energies from previous frame (read)
    //   outBandEnergies      = band energies for this frame  (write)
    //   filterBank           = precomputed triangular filter weights (numBands * numBins)
    //   numBins              = FFT bins (fftSize/2 + 1)
    //   numBands             = number of semitone bands (~86 for A1-B8 at the default range)
    //   maxFilterWidth       = how many adjacent bands to consider for flux
    static float superFlux(const float* currentMag,
                              const float* previousBandEnergies,
                              float* outBandEnergies,
                              const float* filterBank,
                              int numBins,
                              int numBands,
                              int maxFilterWidth = 1);

    // Build precomputed triangular filter bank for superFlux().
    // Call once at engine startup, store the result, pass to superFlux().
    static std::vector<float> buildSemitoneFilterBank(
        int numBins, float sampleRate, int fftSize,
        float minFreqHz = 55.0f,    // A1
        float maxFreqHz = 8000.0f   // ~B8
    );
};



}} // namespace livebpm::dsp