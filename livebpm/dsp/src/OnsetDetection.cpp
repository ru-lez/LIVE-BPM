#include "livebpm/dsp/OnsetDetection.h"
#include <cmath>
#include <algorithm>

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

float OnsetDetection::spectralFlux(const float* currentMag,
                                   const float* previousMag, 
                                   int numBins) {
    float flux = 0.0f;
    for (int k = 0; k < numBins; ++k) {
        float diff = currentMag[k] - previousMag[k];
        // Half-wave rectification, only care about energy increases
        if (diff > 0.0f) {
            flux += diff;
        }
    }
    return flux / static_cast<float>(numBins);
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

}} // namespace livebpm::dsp