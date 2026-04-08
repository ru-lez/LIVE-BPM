#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "livebpm/dsp/TempoEstimation.h"
#include "kiss_fft.h"
#include <algorithm>
#include <cmath>

namespace livebpm { namespace dsp {

TempoEstimation::TempoEstimation(int maxLength)
    : maxLength_(maxLength)
{
    // Round up to next power of 2 for efficient FFT
    int fftSize = 1;
    while (fftSize < maxLength * 2) fftSize <<= 1;
    fftSize_ = fftSize;

    kissConfig_    = kiss_fft_alloc(fftSize, 0, nullptr, nullptr);
    kissInvConfig_ = kiss_fft_alloc(fftSize, 1, nullptr, nullptr);

    fftBuf_    = new kiss_fft_cpx[fftSize];
    fftResult_ = new kiss_fft_cpx[fftSize];
    autocorr_  = new float[fftSize];
}

TempoEstimation::~TempoEstimation() {
    kiss_fft_free(kissConfig_);
    kiss_fft_free(kissInvConfig_);
    delete[] static_cast<kiss_fft_cpx*>(fftBuf_);
    delete[] static_cast<kiss_fft_cpx*>(fftResult_);
    delete[] autocorr_;
}

TempoResult TempoEstimation::estimate(const float* novelty, int length,
                                       double featureRate,
                                       double minBpm, double maxBpm)
{
    if (length < 2) return TempoResult{};

    auto buf    = static_cast<kiss_fft_cpx*>(fftBuf_);
    auto result = static_cast<kiss_fft_cpx*>(fftResult_);

    // --- Step 1: Autocorrelation via FFT (Wiener-Khinchin) ---
    // Zero-pad up to fftSize
    for (int i = 0; i < fftSize_; ++i) {
        buf[i].r = (i < length) ? novelty[i] : 0.0f;
        buf[i].i = 0.0f;
    }

    // Forward FFT
    kiss_fft(static_cast<kiss_fft_cfg>(kissConfig_), buf, result);

    // Power spectrum: |FFT|^2
    for (int k = 0; k < fftSize_; ++k) {
        float re = result[k].r;
        float im = result[k].i;
        buf[k].r = re * re + im * im;
        buf[k].i = 0.0f;
    }

    // Inverse FFT -> autocorrelation
    kiss_fft(static_cast<kiss_fft_cfg>(kissInvConfig_), buf, result);

    // Normalize by lag 0
    float norm = (result[0].r > 0) ? result[0].r : 1.0f;
    for (int i = 0; i < length; ++i) {
        autocorr_[i] = result[i].r / norm;
    }

    // --- Step 2: Find best lag with multiharmonic score ---
    int minLag = static_cast<int>(60.0 / maxBpm * featureRate);
    int maxLag = static_cast<int>(60.0 / minBpm * featureRate);
    minLag = std::max(1, minLag);
    maxLag = std::min(maxLag, length - 1);

    double bestScore = -1e30;
    int bestLag = minLag;

    for (int lag = minLag; lag <= maxLag; ++lag) {
        double score = 1.0 * autocorr_[lag];
        if (lag * 2 < length) score += 0.6  * autocorr_[lag * 2];
        if (lag * 3 < length) score += 0.25 * autocorr_[lag * 3];
        if (score > bestScore) {
            bestScore = score;
            bestLag   = lag;
        }
    }

    // --- Step 3: Parabolic interpolation ---
    double refinedLag = bestLag;
    if (bestLag > 0 && bestLag < length - 1) {
        double alpha = autocorr_[bestLag - 1];
        double beta  = autocorr_[bestLag];
        double gamma = autocorr_[bestLag + 1];
        double denom = alpha - 2.0 * beta + gamma;
        if (std::abs(denom) > 1e-10) {
            double delta = 0.5 * (alpha - gamma) / denom;
            if (std::abs(delta) < 0.5)
                refinedLag = bestLag + delta;
        }
    }

    // --- Step 4: Convert lag to BPM ---
    double bpm = 60.0 * featureRate / refinedLag;
    double strength = std::max(0.0, bestScore);

    return TempoResult{bpm, strength};
}

}} // namespace livebpm::dsp