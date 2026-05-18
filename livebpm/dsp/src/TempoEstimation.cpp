#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "livebpm/dsp/TempoEstimation.h"
#include "kiss_fft.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <memory>

namespace livebpm { namespace dsp {

struct TempoEstimation::Impl {
    int maxLength;
    int fftSize;

    kiss_fft_cfg kissConfig;
    kiss_fft_cfg kissInvConfig;

    std::vector<kiss_fft_cpx> fftBuf;
    std::vector<kiss_fft_cpx> fftResult;
    std::vector<float> autocorr;

    struct LagAnalysis {
        double bestBiasedScore = -1.0;
        double bestRawScore = -1.0;
        double rawWinnerBpm = 0.0;
        int bestLag = 0;
        double finalBpm = 0.0;
    } analysis;

    Impl(int maxLen) : maxLength(maxLen) {
        // Round up to next power of 2 for efficient FFT
        fftSize = 1;
        while (fftSize < maxLength * 2) fftSize <<= 1;

        kissConfig    = kiss_fft_alloc(fftSize, 0, nullptr, nullptr);
        kissInvConfig = kiss_fft_alloc(fftSize, 1, nullptr, nullptr);

        fftBuf.resize(fftSize);
        fftResult.resize(fftSize);
        autocorr.resize(fftSize);
    }

    ~Impl() {
        kiss_fft_free(kissConfig);
        kiss_fft_free(kissInvConfig);
    }

    // --- Step 1: Autocorrelation via FFT (Wiener-Khinchin) ---
    void computeAutocorrelation(const float* novelty, int length) {
        // Zero-pad up to fftSize
        for (int i = 0; i < fftSize; ++i) {
            fftBuf[i].r = (i < length) ? novelty[i] : 0.0f;
            fftBuf[i].i = 0.0f;
        }

        // Forward FFT
        kiss_fft(kissConfig, fftBuf.data(), fftResult.data());

        // Power spectrum: |FFT|^2
        for (int k = 0; k < fftSize; ++k) {
            float re = fftResult[k].r;
            float im = fftResult[k].i;
            fftBuf[k].r = re * re + im * im;
            fftBuf[k].i = 0.0f;
        }

        // Inverse FFT -> autocorrelation
        kiss_fft(kissInvConfig, fftBuf.data(), fftResult.data());

        // Normalize by lag 0
        float norm = (fftResult[0].r > 0) ? fftResult[0].r : 1.0f;
        for (int i = 0; i < length; ++i) {
            autocorr[i] = fftResult[i].r / norm;
        }
    }

    // --- Step 2: Find best lag with multiharmonic score ---
    void evaluateLags(int length, double featureRate,
                      double minBpm, double maxBpm, double lastBpm,
                      double currentConfidence, bool useSmartMixedMode) {
        int minLag = static_cast<int>(60.0 / maxBpm * featureRate);
        int maxLag = static_cast<int>(60.0 / minBpm * featureRate);
        minLag = std::max(1, minLag);
        maxLag = std::min(maxLag, length - 1);

        // Reset analysis state for the new processing frame
        analysis = LagAnalysis{};
        analysis.bestLag = minLag;

        // Use current confidence to flatten the bias curve if signal is weak
        double centerBpm = (lastBpm > 0.0) ? lastBpm : 100.0;
        double dynamicWidth = 1.1 + (3.0 * (1.0 - currentConfidence));

        for (int lag = minLag; lag <= maxLag; ++lag) {
            double rawScore = autocorr[lag];
            double currentBpm = 60.0 * featureRate / lag;

            // If smart mode is enabled, fold the 8th and 16th note energy back into the 1/4 note score
            if (useSmartMixedMode) {
                int lag8th = lag / 2;
                if (lag8th > 0) {
                    rawScore += (autocorr[lag8th] * 0.5); 
                }

                int lag16th = lag / 4;
                if (lag16th > 0) {
                    rawScore += (autocorr[lag16th] * 0.25); 
                }

                if (currentBpm > 135.0) {
                    int lagHalfTime = lag * 2;
                    if (lagHalfTime < length) {
                        if (autocorr[lagHalfTime] > (autocorr[lag] * 0.4)) {
                            rawScore *= 0.5;
                        }
                    }
                }
            }

            // Log-Gaussian Weighting
            double weight = std::exp(-0.5 * std::pow(std::log2(currentBpm / centerBpm) / dynamicWidth, 2.0));
            double biasedScore = rawScore * weight;

            // Track the strongest peak for breakthrough logic
            if (rawScore > analysis.bestRawScore) {
                analysis.bestRawScore = rawScore;
                analysis.rawWinnerBpm = currentBpm;
            }

            // Track the winner according to the bias
            if (biasedScore > analysis.bestBiasedScore) {
                analysis.bestBiasedScore = biasedScore;
                analysis.bestLag = lag;
            }
        }

        // Calculate the BPM of our biased winner
        analysis.finalBpm = 60.0 * featureRate / analysis.bestLag;
    }

    // --- Step 3: Breakthrough Logic for Strong Unbiased Peaks ---
    bool applyBreakthroughLogic() {
        double biasedWinnerRawScore = autocorr[analysis.bestLag];
        
        if (std::abs(analysis.rawWinnerBpm - (analysis.finalBpm * 2.0)) < 10.0) {
            if (analysis.bestRawScore > biasedWinnerRawScore * 0.95) {
                analysis.finalBpm = analysis.rawWinnerBpm;
                return true;
            }
        }
        return false;
    }

    // --- Step 4: Parabolic interpolation ---
    double refineLag(int length) {
        double refinedLag = analysis.bestLag;
        if (analysis.bestLag > 0 && analysis.bestLag < length - 1) {
            double alpha = autocorr[analysis.bestLag - 1];
            double beta  = autocorr[analysis.bestLag];
            double gamma = autocorr[analysis.bestLag + 1];
            double denom = alpha - 2.0 * beta + gamma;
            if (std::abs(denom) > 1e-10) {
                double delta = 0.5 * (alpha - gamma) / denom;
                if (std::abs(delta) < 0.5)
                    refinedLag = analysis.bestLag + delta;
            }
        }
        return refinedLag;
    }
};


TempoEstimation::TempoEstimation(int maxLength) : pImpl_(std::make_unique<Impl>(maxLength)) {}

TempoEstimation::~TempoEstimation() = default;

TempoResult TempoEstimation::estimate(const float* novelty, int length, double featureRate,
                                      double minBpm, double maxBpm, double lastBpm,
                                      double currentConfidence, bool useSmartMixedMode) {
    if (length < 2) return TempoResult{};

    auto& d = *pImpl_;

    // --- Step 1: Autocorrelation via FFT (Wiener-Khinchin) ---
    d.computeAutocorrelation(novelty, length);
    // --- Step 2: Find best lag with multiharmonic score ---
    d.evaluateLags(length, featureRate, minBpm, maxBpm, lastBpm, currentConfidence, useSmartMixedMode);

    // Disable breakthrough logic in SmartMixed mode
    if (!useSmartMixedMode) {
        // --- Step 3: Breakthrough Logic for Strong Unbiased Peaks ---
        if (d.applyBreakthroughLogic()) {
            return TempoResult{ d.analysis.finalBpm, static_cast<float>(d.analysis.bestRawScore) };
        }
    }

    // --- Step 4: Parabolic interpolation ---
    double refinedLag = d.refineLag(length);

    // --- Step 5: Calculate final BPM ---
    double finalBpm = 60.0 * featureRate / refinedLag;
    float strength = static_cast<float>(std::max(0.0, d.analysis.bestBiasedScore));

    return TempoResult{ finalBpm, strength };
}

}} // namespace livebpm::dsp