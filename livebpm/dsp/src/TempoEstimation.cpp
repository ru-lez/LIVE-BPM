#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "livebpm/dsp/TempoEstimation.h"
#include "kiss_fft.h"
#include <algorithm>
#include <cmath>
#include <iostream>

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
                                       double minBpm, double maxBpm,
                                       double lastBpm, double currentConfidence,
                                       bool useSmartMixedMode)
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

    double bestBiasedScore = -1.0;
    double bestRawScore = -1.0;
    double rawWinnerBpm = 0.0;
    int bestLag = minLag;

    // Use current confidence to flatten the bias curve if signal is weak
    double centerBpm = (lastBpm > 0.0) ? lastBpm : 100.0;
    double dynamicWidth = 1.1 + (3.0 * (1.0 - currentConfidence));

    for (int lag = minLag; lag <= maxLag; ++lag) {
        double rawScore = autocorr_[lag];
        double currentBpm = 60.0 * featureRate / lag;

        // If smart mode is enabled, fold the 8th and 16th note energy back into the 1/4 note score
        if (useSmartMixedMode) {
            int lag8th = lag / 2;
            if (lag8th > 0) {
                rawScore += (autocorr_[lag8th] * 0.5); 
            }

            int lag16th = lag / 4;
            if (lag16th > 0) {
                rawScore += (autocorr_[lag16th] * 0.25); 
            }

            if (currentBpm > 135.0) {
                int lagHalfTime = lag * 2;
                if (lagHalfTime < length) {
                    if (autocorr_[lagHalfTime] > (autocorr_[lag] * 0.4)) {
                        rawScore *= 0.5;
                    }
                }
            }
        }

        // Log-Gaussian Weighting
        double weight = std::exp(-0.5 * std::pow(std::log2(currentBpm / centerBpm) / dynamicWidth, 2.0));
        double biasedScore = rawScore * weight;

        // Track the strongest peak for breakthrough logic
        if (rawScore > bestRawScore) {
            bestRawScore = rawScore;
            rawWinnerBpm = currentBpm;
        }

        // Track the winner according to the bias
        if (biasedScore > bestBiasedScore) {
            bestBiasedScore = biasedScore;
            bestLag = lag;
        }
    }

    // Calculate the BPM of our biased winner
    double finalBpm = 60.0 * featureRate / bestLag;

    // Disable breakthrough logic in SmartMixed mode
    if (!useSmartMixedMode) {
        // --- Step 3: Breakthrough Logic for Strong Unbiased Peaks ---
        double biasedWinnerRawScore = autocorr_[bestLag];
        
        if (std::abs(rawWinnerBpm - (finalBpm * 2.0)) < 10.0) {
            if (bestRawScore > biasedWinnerRawScore * 0.95) {
                finalBpm = rawWinnerBpm;
                return TempoResult{ finalBpm, static_cast<float>(bestRawScore) };
            }
        }
    }

    // --- Step 4: Parabolic interpolation ---
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

    // --- Step 5 ---
    finalBpm = 60.0 * featureRate / refinedLag;
    float strength = static_cast<float>(std::max(0.0, bestBiasedScore));

    return TempoResult{ finalBpm, strength };
}

}} // namespace livebpm::dsp