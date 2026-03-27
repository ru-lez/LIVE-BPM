#pragma once

#include <vector>

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

    TempoResult estimate(const float* novelty, 
                         int length,
                         double featureRate,
                         double minBpm, 
                         double maxBpm);

private:
    int maxLength_;
    void* kissConfig_;    // for autocorrelation FFT
    void* kissInvConfig_; // for IFFT
    std::vector<float> autocorr_;
};

}} // namespace livebpm::dsp