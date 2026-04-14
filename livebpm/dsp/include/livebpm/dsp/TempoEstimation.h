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

    TempoResult estimate(const float* novelty, int length,
                         double featureRate,
                         double minBpm, double maxBpm);

private:
    int maxLength_;
    int fftSize_;
    void* kissConfig_;
    void* kissInvConfig_;
    void* fftBuf_;    // allocated as kiss_fft_cpx[]
    void* fftResult_; // allocated as kiss_fft_cpx[]
    float* autocorr_;
};

}} // namespace livebpm::dsp