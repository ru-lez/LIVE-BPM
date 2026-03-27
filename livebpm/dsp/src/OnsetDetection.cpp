#include "livebpm/dsp/OnsetDetection.h"

namespace livebpm {
namespace dsp {

float OnsetDetection::spectralFlux(const float* /*currentMag*/,
                                   const float* /*previousMag*/,
                                   int /*numBins*/) {
    return 0.0f; // TODO: implement in Week 2
}

}} // namespace livebpm::dsp