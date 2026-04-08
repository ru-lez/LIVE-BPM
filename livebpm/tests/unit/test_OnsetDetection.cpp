#include <catch2/catch_test_macros.hpp>

#include "livebpm/dsp/SpectralAnalysis.h"
#include "livebpm/dsp/OnsetDetection.h"
#include "TestSignals.h"

#include <vector>
#include <algorithm>

using namespace livebpm::dsp;
using namespace livebpm::test;

TEST_CASE("OnsetDetection: identical frames produce zero flux", "[onset]") {
    std::vector<float> mag = {0.5f, 0.3f, 0.1f, 0.0f};

    float flux = OnsetDetection::spectralFlux(
        mag.data(),
        mag.data(),
        static_cast<int>(mag.size())
    );

    REQUIRE(flux == 0.0f);
}

TEST_CASE("OnsetDetection: increasing magnitude produces positive flux", "[onset]") {
    std::vector<float> prev = {0.0f, 0.0f, 0.0f};
    std::vector<float> curr = {1.0f, 1.0f, 1.0f};

    float flux = OnsetDetection::spectralFlux(
        curr.data(),
        prev.data(),
        3
    );

    REQUIRE(flux > 0.0f);
}

TEST_CASE("OnsetDetection: decreasing magnitude produces zero flux", "[onset]") {
    std::vector<float> prev = {1.0f, 1.0f, 1.0f};
    std::vector<float> curr = {0.0f, 0.0f, 0.0f};

    // Half-wave rectification: decreases don't count
    float flux = OnsetDetection::spectralFlux(
        curr.data(),
        prev.data(),
        3
    );

    REQUIRE(flux == 0.0f);
}

TEST_CASE("OnsetDetection: click track produces periodic flux peaks", "[onset][integration]") {
    const int fftSize = 2048;
    const int hop = 512;
    const double sampleRate = 48000.0;
    const double bpm = 120.0;

    // Generate click track
    auto signal = generateClickTrack(bpm, sampleRate, 3.0);

    SpectralAnalysis sa(fftSize);
    int numBins = sa.getNumBins();

    std::vector<float> prevMag(numBins, 0.0f);
    std::vector<float> currMag(numBins);
    std::vector<float> phase(numBins);
    std::vector<float> fluxValues;

    // Process signal in hops
    for (size_t offset = 0; offset + fftSize <= signal.size(); offset += hop) {
        sa.compute(&signal[offset], currMag.data(), phase.data());

        float flux = OnsetDetection::spectralFlux(
            currMag.data(),
            prevMag.data(),
            numBins
        );

        fluxValues.push_back(flux);

        std::copy(currMag.begin(), currMag.end(), prevMag.begin());
    }

    // Verify flux peaks exist
    float maxFlux = *std::max_element(
        fluxValues.begin(),
        fluxValues.end()
    );

    REQUIRE(maxFlux > 0.0f);

    // Count significant peaks (>30% of max)
    float threshold = maxFlux * 0.3f;
    int peakCount = 0;

    for (size_t i = 1; i + 1 < fluxValues.size(); ++i) {
        if (fluxValues[i] > threshold &&
            fluxValues[i] > fluxValues[i - 1] &&
            fluxValues[i] >= fluxValues[i + 1]) {
            ++peakCount;
        }
    }

    // At 120 BPM in 3 seconds: ~6 beats
    REQUIRE(peakCount >= 4);
}