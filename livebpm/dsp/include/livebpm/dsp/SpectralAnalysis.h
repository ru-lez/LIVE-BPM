#pragma once

#include <vector>
#include <kiss_fft.h>

namespace livebpm {
namespace dsp {

class SpectralAnalysis {
public:
    explicit SpectralAnalysis(int fftSize);
    ~SpectralAnalysis();

    void compute(const float* samples,
                 float* magnitude,
                 float* phase);

    int getNumBins() const { return fftSize_ / 2 + 1; }
    int getFftSize() const { return fftSize_; }

private:
    int fftSize_;

    std::vector<float> hannWindow_;

    kiss_fft_cfg kissConfig_;
    std::vector<kiss_fft_cpx> fftIn_;
    std::vector<kiss_fft_cpx> fftOut_;
};

} // namespace dsp
} // namespace livebpm