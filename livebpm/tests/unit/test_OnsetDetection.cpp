#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "livebpm/dsp/SpectralAnalysis.h"
#include "livebpm/dsp/OnsetDetection.h"
#include "TestSignals.h"
#include <algorithm>

using namespace livebpm::dsp;
using namespace livebpm::test;

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

TEST_CASE("OnsetDetection: buildSemitoneFilterBank generates correct dimensions", "[onset]") {
    int numBins = 513; // 1024 FFT
    float sampleRate = 48000.0f;
    int fftSize = 1024;
    float minFreq = 27.5f; // A0
    float maxFreq = 4186.0f; // C8

    auto bank = OnsetDetection::buildSemitoneFilterBank(numBins, sampleRate, fftSize, minFreq, maxFreq);
    
    // Calculate expected number of bands based on 12 semitones per octave
    int expectedBands = static_cast<int>(std::round(12.0f * std::log2(maxFreq / minFreq)));
    
    REQUIRE(bank.size() == expectedBands * numBins);

    // Check that filters are non-negative
    for (float weight : bank) {
        REQUIRE(weight >= 0.0f);
    }
}

TEST_CASE("OnsetDetection: superFlux calculates positive derivatives accurately", "[onset]") {
    int numBins = 4;
    int numBands = 2;
    std::vector<float> currentMag = {1.0f, 2.0f, 0.5f, 0.1f};
    std::vector<float> prevBands = {0.3f, 0.7f};
    
    // Mock filter bank
    std::vector<float> filterBank = {
        1.0f, 0.5f, 0.0f, 0.0f, // Band 0
        0.0f, 0.5f, 1.0f, 0.0f  // Band 1
    };
    std::vector<float> outBands(numBands, 0.0f);

    float flux = OnsetDetection::superFlux(currentMag.data(), prevBands.data(), outBands.data(), filterBank.data(), numBins, numBands, 1);

    // Band 0 expected energy: sqrt((1*1)^2 + (2*0.5)^2) = sqrt(1 + 1) = 1.4142
    REQUIRE_THAT(outBands[0], Catch::Matchers::WithinRel(1.4142f, 0.01f));
    
    // Band 1 expected energy: sqrt((2*0.5)^2 + (0.5*1)^2) = sqrt(1 + 0.25) = 1.1180
    REQUIRE_THAT(outBands[1], Catch::Matchers::WithinRel(1.1180f, 0.01f));
    
    // Expected Flux:
    // Total energy = 1.4142 + 1.1180 = 2.5322
    //
    // Maximum Filter (width = 1):
    // Band 0 local max = max(prevBands[0], prevBands[1]) = max(0.3, 0.7) = 0.7
    // Band 1 local max = max(prevBands[0], prevBands[1]) = max(0.3, 0.7) = 0.7
    //
    // Positive diffs:
    // Band 0: max(0, 1.4142 - 0.7) = 0.7142
    // Band 1: max(0, 1.1180 - 0.7) = 0.4180
    // Total positive diffs = 0.7142 + 0.4180 = 1.1322
    //
    // Normalised Flux = 1.1322 / 2.5322 = 0.4471
    REQUIRE_THAT(flux, Catch::Matchers::WithinRel(0.4471f, 0.01f));
}