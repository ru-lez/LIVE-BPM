#pragma once

#include <vector>
#include <cmath>

// Forward declaration substituted due to MSVC linker issues with kiss_fft_cpx
#include "kiss_fft.h"

namespace livebpm { namespace dsp {

class SpectralAnalysis {
public:
    explicit SpectralAnalysis(int fftSize);
    ~SpectralAnalysis();

    // Applies Hann window + FFT. Writes to magnitude and phase.
    void compute(const float* samples,
                 float* magnitude,
                 float* phase);

    int getNumBins() const { return fftSize_ / 2 + 1; }
    int getFftSize() const { return fftSize_; }

private:
    int fftSize_;
    std::vector<float> hannWindow_;
    void* kissConfig_;
    std::vector<kiss_fft_cpx> fftIn_;
    std::vector<kiss_fft_cpx> fftOut_;
};

}} // namespace livebpm::dsp