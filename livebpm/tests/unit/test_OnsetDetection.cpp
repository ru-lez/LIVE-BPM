#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "livebpm/dsp/SpectralAnalysis.h"
#include "livebpm/dsp/OnsetDetection.h"
#include "TestSignals.h"
#include <algorithm>

using namespace livebpm::dsp;
using namespace livebpm::test;

TEST_CASE("OnsetDetection: identical frames produce zero flux", "[onset]") {
    std::vector<float> mag = {0.5f, 0.5f, 0.5f};
    float flux = OnsetDetection::spectralFlux(mag.data(), mag.data(), static_cast<int>(mag.size()));
    REQUIRE(flux == 0.0f);
}

TEST_CASE("OnsetDetection: increasing magnitude produces positive flux", "[onset]") {
    std::vector<float> prev = {0.0f, 0.0f, 0.0f};
    std::vector<float> curr = {1.0f, 1.0f, 1.0f};
    float flux = OnsetDetection::spectralFlux(curr.data(), prev.data(), 3);
    REQUIRE(flux > 0.0f);
}

TEST_CASE("OnsetDetection: decreasing magnitude produces zero flux", "[onset]") {
    std::vector<float> prev = {1.0f, 1.0f, 1.0f};
    std::vector<float> curr = {0.0f, 0.0f, 0.0f};
    // Half-wave rectification: decreases don't count
    float flux = OnsetDetection::spectralFlux(curr.data(), prev.data(), 3);
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
    std::vector<float> currMag(numBins, 0.0f);
    std::vector<float> phase(numBins, 0.0f);
    std::vector<float> fluxValues;

    // Process by hops
    for (size_t offset = 0; offset + fftSize <= signal.size(); offset += hop) {
        sa.compute(&signal[offset], currMag.data(), phase.data());
        float flux = OnsetDetection::spectralFlux(currMag.data(), prevMag.data(), numBins);
        fluxValues.push_back(flux);
        std::copy(currMag.begin(), currMag.end(), prevMag.begin());
    }

    // Verify that there are flux peaks
    float maxFlux = *std::max_element(fluxValues.begin(), fluxValues.end());
    REQUIRE(maxFlux > 0.0f);

    // Count significant peaks (> 30% of maximum)
    float threshold = maxFlux * 0.3f;
    int peakCount = 0;
    for (size_t i = 1; i < fluxValues.size() - 1; ++i) {
        if (fluxValues[i] > threshold && 
            fluxValues[i] > fluxValues[i - 1] && 
            fluxValues[i] >= fluxValues[i + 1]) {
            ++peakCount;
        }
    }

    // At 120 BPM in 3 seconds: 6 beats. We should detect at least 4.
    REQUIRE(peakCount >= 4);
}

TEST_CASE("OnsetDetection: complexDomain ground truth - steady state", "[onset][complex]") {
    const int numBins = 1;
    float currMag[] = { 1.0f };
    float prevMag[] = { 1.0f };
    float currPhase[] = { 1.0f };
    float prevPhase[] = { 0.5f };
    float prevPrevPhase[] = { 0.0f };

    float cd = OnsetDetection::complexDomain(currMag, currPhase, prevMag, prevPhase, prevPrevPhase, numBins);
    
    // With perfect prediction, CD should be exactly 0
    REQUIRE_THAT(cd, Catch::Matchers::WithinAbs(0.0, 1e-6));
}

TEST_CASE("OnsetDetection: complexDomain ground truth - phase jump", "[onset][complex]") {
    const int numBins = 1;
    float currMag[] = { 1.0f };
    float prevMag[] = { 1.0f };
    
    // Linear trajectory predicts 1.0, but we provide 1.0 + PI
    float prevPrevPhase[] = { 0.0f };
    float prevPhase[] = { 0.5f };
    float currPhase[] = { 1.0f + 3.1415926535f }; 

    float cd = OnsetDetection::complexDomain(currMag, currPhase, prevMag, prevPhase, prevPrevPhase, numBins);
    
    // Expected distance for a PI shift with Mag=1.0 is 2.0
    REQUIRE_THAT(cd, Catch::Matchers::WithinAbs(2.0, 1e-5));
}

TEST_CASE("OnsetDetection: complexDomain ground truth - magnitude step", "[onset][complex]") {
    const int numBins = 1;
    // Magnitude doubles, phase remains perfectly predictable
    float currMag[] = { 2.0f };
    float prevMag[] = { 1.0f };
    float currPhase[] = { 1.0f };
    float prevPhase[] = { 0.5f };
    float prevPrevPhase[] = { 0.0f };

    float cd = OnsetDetection::complexDomain(currMag, currPhase, prevMag, prevPhase, prevPrevPhase, numBins);
    
    // Expected distance: sqrt(2^2 + 1^2 - 2*2*1*cos(0)) = sqrt(5 - 4) = 1.0
    REQUIRE_THAT(cd, Catch::Matchers::WithinAbs(1.0, 1e-6));
}

TEST_CASE("OnsetDetection: complexDomain normalisation scales with bin count", "[onset][complex]") {
    const int numBins = 2;
    float currMag[] = { 1.0f, 1.0f };
    float prevMag[] = { 1.0f, 1.0f };
    float currPhase[] = { 1.0f + 3.1415926535f, 1.0f + 3.1415926535f }; 
    float prevPhase[] = { 0.5f, 0.5f };
    float prevPrevPhase[] = { 0.0f, 0.0f };

    float cd = OnsetDetection::complexDomain(currMag, currPhase, prevMag, prevPhase, prevPrevPhase, numBins);
    
    // Individual bin distance is 2.0. Total distance is 4.0. Normalised (divided by 2 bins) is 2.0
    REQUIRE_THAT(cd, Catch::Matchers::WithinAbs(2.0, 1e-5));
}