#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "livebpm/dsp/SpectralAnalysis.h"
#include "TestSignals.h"
#include <vector>
#include <algorithm>

using namespace livebpm::dsp;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

TEST_CASE("SpectralAnalysis: constructor pre-allocates correctly", "[spectral]") {
    SpectralAnalysis sa(2048);
    REQUIRE(sa.getFftSize() == 2048);
    REQUIRE(sa.getNumBins() == 1025);
}

TEST_CASE("SpectralAnalysis: different FFT sizes", "[spectral]") {
    SpectralAnalysis sa512(512);
    REQUIRE(sa512.getNumBins() == 257);
    SpectralAnalysis sa1024(1024);
    REQUIRE(sa1024.getNumBins() == 513);
}

TEST_CASE("SpectralAnalysis: pure sine produces peak at correct bin", "[spectral]") {
    const int fftSize = 2048;
    const double sampleRate = 48000.0;
    const double freq = 440.0; // A440 note

    SpectralAnalysis sa(fftSize);
    std::vector<float> samples(fftSize);
    
    // Generate pure sinusoid
    for (int i = 0; i < fftSize; ++i) {
        samples[i] = std::sin(2.0 * M_PI * freq * i / sampleRate);
    }

    std::vector<float> magnitude(sa.getNumBins());
    std::vector<float> phase(sa.getNumBins());
    sa.compute(samples.data(), magnitude.data(), phase.data());

    // The expected bin for 440 Hz
    double binHz = sampleRate / fftSize; // ~23.44 Hz per bin
    int expectedBin = static_cast<int>(std::round(freq / binHz));

    // Find the bin with the highest magnitude
    int peakBin = 0;
    float peakMag = 0.0f;
    for (int k = 1; k < sa.getNumBins(); ++k) { // skip DC at bin 0
        if (magnitude[k] > peakMag) {
            peakMag = magnitude[k];
            peakBin = k;
        }
    }

    // The peak must be at the correct bin (+/- 1 due to windowing)
    REQUIRE(std::abs(peakBin - expectedBin) <= 1);
    // The peak magnitude must be significantly greater than other bins
    REQUIRE(peakMag > 0.05f);
}

TEST_CASE("SpectralAnalysis: silence produces near-zero magnitude", "[spectral]") {
    const int fftSize = 2048;
    SpectralAnalysis sa(fftSize);
    std::vector<float> silence(fftSize, 0.0f);
    std::vector<float> magnitude(sa.getNumBins());
    std::vector<float> phase(sa.getNumBins());

    sa.compute(silence.data(), magnitude.data(), phase.data());
    
    float maxMag = *std::max_element(magnitude.begin(), magnitude.end());
    REQUIRE(maxMag < 1e-6f);
}

TEST_CASE("SpectralAnalysis: DC signal produces energy only in bin 0", "[spectral]") {
    const int fftSize = 2048;
    SpectralAnalysis sa(fftSize);
    std::vector<float> dc(fftSize, 0.5f); // DC offset
    std::vector<float> magnitude(sa.getNumBins());
    std::vector<float> phase(sa.getNumBins());

    sa.compute(dc.data(), magnitude.data(), phase.data());
    
    // Bin 0 (DC) should have the most energy
    float dcMag = magnitude[0];
    REQUIRE(dcMag > 0.0f);
    
    // High bins should have much less energy than DC
    float highBinMag = magnitude[sa.getNumBins() / 2];
    REQUIRE(dcMag > highBinMag * 10.0f);
}

TEST_CASE("SpectralAnalysis: impulse distributes energy across all bins", "[spectral]") {
    const int fftSize = 1024;
    SpectralAnalysis sa(fftSize);
    std::vector<float> impulse(fftSize, 0.0f);
    
    // Create an impulse at the start of window
    impulse[0] = 1.0f; 

    std::vector<float> magnitude(sa.getNumBins());
    std::vector<float> phase(sa.getNumBins());

    sa.compute(impulse.data(), magnitude.data(), phase.data());
    
    // Energy must be present in all bins
    bool allBinsHaveEnergy = true;
    for (int k = 0; k < sa.getNumBins(); ++k) {
        if (magnitude[k] < 1e-6f) {
            allBinsHaveEnergy = false;
            break;
        }
    }
    
    REQUIRE(allBinsHaveEnergy);
}