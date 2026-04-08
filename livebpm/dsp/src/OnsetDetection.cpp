#include "livebpm/dsp/OnsetDetection.h"
#include <cmath>

namespace livebpm {
namespace dsp {

float OnsetDetection::complexDomain(
    const float* currentMag,
    const float* currentPhase,
    const float* prevMag,
    const float* prevPhase,
    const float* prevPrevPhase,
    int numBins)
{
    float onset = 0.0f;

    for (int k = 0; k < numBins; ++k) {

        // Phase prediction
        float predictedPhase = 2.0f * prevPhase[k] - prevPrevPhase[k];

        // Convert polar to complex
        float realPred = prevMag[k] * std::cos(predictedPhase);
        float imagPred = prevMag[k] * std::sin(predictedPhase);

        float realCurr = currentMag[k] * std::cos(currentPhase[k]);
        float imagCurr = currentMag[k] * std::sin(currentPhase[k]);

        // Euclidean distance in complex domain
        float diffReal = realCurr - realPred;
        float diffImag = imagCurr - imagPred;

        onset += std::sqrt(diffReal * diffReal + diffImag * diffImag);
    }

    return onset;
}

} // namespace dsp
} // namespace livebpm