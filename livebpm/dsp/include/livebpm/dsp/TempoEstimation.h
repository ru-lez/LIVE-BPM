#pragma once

#include <vector>
#include <memory>

namespace livebpm {
namespace dsp {

struct TempoResult {
    double bpm = 0.0;
    double strength = 0.0; // autocorrelation peak strength
};

class TempoEstimation {
public:
    explicit TempoEstimation(int maxNoveltyLength);
    ~TempoEstimation();

    TempoResult estimate(const float* novelty, int length,
                         double featureRate,
                         double minBpm, double maxBpm,
                         double lastBpm, double currentConfidence,
                         bool useSmartMixedMode = false);

private:
    struct Impl; 
    std::unique_ptr<Impl> pImpl_; 
};

}} // namespace livebpm::dsp