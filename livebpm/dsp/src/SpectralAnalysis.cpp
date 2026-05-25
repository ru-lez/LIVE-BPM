// Ignoring third-party libraries for -Wall -Wextra -Wpedantic
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "kiss_fft.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "livebpm/dsp/SpectralAnalysis.h"

namespace livebpm {
namespace dsp {

SpectralAnalysis::SpectralAnalysis(int fftSize)
    : fftSize_(fftSize)
    , hannWindow_(fftSize)
    , fftIn_(fftSize)
    , fftOut_(fftSize) {
    
    // Pre-compute Hann window
    for (int i = 0; i < fftSize; ++i) {
        hannWindow_[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (fftSize - 1)));
    }
    kissConfig_ = kiss_fft_alloc(fftSize, 0, nullptr, nullptr);
}

SpectralAnalysis::~SpectralAnalysis() {
    kiss_fft_free(kissConfig_);
}

void SpectralAnalysis::compute(const float* samples, 
                               float* magnitude, 
                               float* phase) {
    auto cfg = static_cast<kiss_fft_cfg>(kissConfig_);

    // Apply window and copy to complex input buffer
    for (int i = 0; i < fftSize_; ++i) {
        fftIn_[i].r = samples[i] * hannWindow_[i];
        fftIn_[i].i = 0.0f;
    }

    // FFT
    kiss_fft(cfg, fftIn_.data(), fftOut_.data());

    // Extract magnitude and phase (only first half + 1)
    int numBins = fftSize_ / 2 + 1;
    for (int k = 0; k < numBins; ++k) {
        float re = fftOut_[k].r;
        float im = fftOut_[k].i;
        magnitude[k] = std::sqrt(re * re + im * im);
        phase[k] = std::atan2(im, re);
    }
}

}} // namespace livebpm::dsp