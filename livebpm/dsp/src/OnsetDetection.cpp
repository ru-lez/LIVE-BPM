#include "livebpm/dsp/OnsetDetection.h"

#include <algorithm>

namespace livebpm {
namespace dsp {

float OnsetDetection::spectralFlux(const float* currentMag, 
                                   const float* previousMag, 
                                   int numBins) {
    float flux = 0.0f;

    for (int k = 0; k < numBins; ++k) {
        float diff = currentMag[k] - previousMag[k];
        
        if (diff > 0.0f) {
            flux += diff;
        }
    }

    // Normalize by number of bins so the value is independent of FFT size
    return flux / static_cast<float>(numBins);
}

}} // namespace livebpm::dsp