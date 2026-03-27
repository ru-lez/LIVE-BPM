#include "livebpm/dsp/TempoEstimation.h"

namespace livebpm {
namespace dsp {

TempoEstimation::TempoEstimation(int maxLength) 
    : maxLength_(maxLength) {
    // TODO: allocate FFT for autocorrelation
}

TempoEstimation::~TempoEstimation() = default;

TempoResult TempoEstimation::estimate(const float* /*novelty*/, 
                                      int /*length*/,
                                      double /*featureRate*/,
                                      double /*minBpm*/, 
                                      double /*maxBpm*/) {
    return TempoResult{}; // TODO: implement in Week 3
}

}} // namespace livebpm::dsp