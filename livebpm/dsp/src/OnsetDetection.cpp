#include "livebpm/dsp/OnsetDetection.h"
#include <cmath>
#include <algorithm>
#include <vector>


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace livebpm { namespace dsp {

// Helper to wrap a phase value (in radians) into the principal range [-PI, PI].
static float princarg(float phase) {
    while (phase <= -M_PI) phase += 2.0f * (float)M_PI;
    while (phase > (float)M_PI) phase -= 2.0f * (float)M_PI;
    return phase;
}

// Complex Domain Onset Detection
// Detects onsets by measuring the Euclidean distance between the predicted complex bin and the actual measured complex bin.
float OnsetDetection::complexDomain(const float* currentMag, const float* currentPhase,
                                    const float* prevMag, const float* prevPhase,
                                    const float* prevPrevPhase, int numBins) {
    float cdValue = 0.0f;

    for (int k = 0; k < numBins; ++k) {
        // Predict the target phase for the current frame
        float targetPhase = princarg(2.0f * prevPhase[k] - prevPrevPhase[k]);
        
        // Calculate the phase difference between the measurement and our prediction
        float phaseDiff = princarg(currentPhase[k] - targetPhase);
        
        // Compute Euclidean distance in the complex plane
        // Assume the target magnitude is equal to the previous frame's magnitude
        float distSq = currentMag[k] * currentMag[k] + 
                       prevMag[k] * prevMag[k] - 
                       2.0f * currentMag[k] * prevMag[k] * std::cos(phaseDiff);

        // Add the distance (sqrt) to our detection function
        cdValue += std::sqrt(std::max(0.0f, distSq));
    }

    // Normalise by number of bins for consistency across different FFT sizes
    return cdValue / static_cast<float>(numBins);
}


float OnsetDetection::semitoneFlux(const float* currentMag,
                                    const float* previousBandEnergies,
                                    float* outBandEnergies,
                                    const float* filterBank,
                                    int numBins,
                                    int numBands)
{
    // Step 1: Apply each triangular filter to get band energies
    // b_i = sqrt( sum_k (|X[k]| * |H_i[k]|)^2 )
    for (int i = 0; i < numBands; ++i) {
        const float* filter = &filterBank[i * numBins];
        float sumSquared = 0.0f;
        for (int k = 0; k < numBins; ++k) {
            float v = currentMag[k] * filter[k];
            sumSquared += v * v;
        }
        outBandEnergies[i] = std::sqrt(sumSquared);
    }

    // Step 2: Compute positive first-order derivatives across all bands
    // a[t] = sum max(0, b_i[t] - b_i[t-1])
    float positiveDerivatives = 0.0f;
    float totalEnergy = 0.0f;
    for (int i = 0; i < numBands; ++i) {
        float diff = outBandEnergies[i] - previousBandEnergies[i];
        if (diff > 0.0f) {
            positiveDerivatives += diff;
        }
        totalEnergy += outBandEnergies[i];
    }

    // Step 3: Normalise by total band energy (paper's o[t] = a[t] / s[t])
    if (totalEnergy < 1e-6f) return 0.0f;
    return positiveDerivatives / totalEnergy;
}

// Build the triangular semitone filter bank
// Each filter is centred on a musical pitch frequency.
// Returns numBands filters, each of size numBins.
//
// Layout: filterBank[i * numBins + k] is filter i, bin k
std::vector<float> OnsetDetection::buildSemitoneFilterBank(
    int numBins, float sampleRate, int fftSize,
    float minFreqHz, float maxFreqHz)
{
    // Calculate how many semitones fit between min and max
    int numBands = static_cast<int>(
        std::round(12.0f * std::log2(maxFreqHz / minFreqHz))
    );

    std::vector<float> filterBank(numBands * numBins, 0.0f);

    float binHz = sampleRate / fftSize;

    for (int i = 0; i < numBands; ++i) {
        // Center frequency of this band (semitone i above minFreqHz)
        float centerHz = minFreqHz * std::pow(2.0f, i / 12.0f);

        // Lower and upper edges (one semitone below/above center)
        float lowerHz = minFreqHz * std::pow(2.0f, (i - 1) / 12.0f);
        float upperHz = minFreqHz * std::pow(2.0f, (i + 1) / 12.0f);

        // Build the triangular filter for this band
        for (int k = 0; k < numBins; ++k) {
            float binFreq = k * binHz;

            if (binFreq < lowerHz || binFreq > upperHz) {
                filterBank[i * numBins + k] = 0.0f;
            } else if (binFreq <= centerHz) {
                // Rising edge of triangle
                filterBank[i * numBins + k] =
                    (binFreq - lowerHz) / (centerHz - lowerHz);
            } else {
                // Falling edge of triangle
                filterBank[i * numBins + k] =
                    (upperHz - binFreq) / (upperHz - centerHz);
            }
        }
    }

    return filterBank;
}

}} // namespace livebpm::dsp