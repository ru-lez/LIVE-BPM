#include <catch2/catch_test_macros.hpp>
#include "livebpm/dsp/TempoEstimation.h"
#include "TestSignals.h"
#include <cmath>

using namespace livebpm::dsp;
using namespace livebpm::test;

TEST_CASE("TempoEstimation: 120 BPM synthetic novelty", "[tempo]") {
    double featureRate = 93.75; // 48000 / 512
    auto novelty = generatePeriodicNovelty(120.0, featureRate, 5.0);

    TempoEstimation te(static_cast<int>(novelty.size()));
    auto result = te.estimate(
        novelty.data(),
        static_cast<int>(novelty.size()),
        featureRate,
        40.0,
        240.0,
        0.0,
        0.0
    );

    REQUIRE(result.bpm > 115.0);
    REQUIRE(result.bpm < 125.0);
    REQUIRE(result.strength > 0.3);
}

TEST_CASE("TempoEstimation: 60 BPM synthetic novelty", "[tempo]") {
    double featureRate = 93.75;
    auto novelty = generatePeriodicNovelty(60.0, featureRate, 8.0);

    TempoEstimation te(static_cast<int>(novelty.size()));
    auto result = te.estimate(
        novelty.data(),
        static_cast<int>(novelty.size()),
        featureRate,
        40.0,
        240.0,
        0.0,
        0.0
    );

    REQUIRE(result.bpm > 55.0);
    REQUIRE(result.bpm < 65.0);
}

TEST_CASE("TempoEstimation: 180 BPM synthetic novelty", "[tempo]") {
    double featureRate = 93.75;
    auto novelty = generatePeriodicNovelty(180.0, featureRate, 4.0);

    TempoEstimation te(static_cast<int>(novelty.size()));
    auto result = te.estimate(
        novelty.data(),
        static_cast<int>(novelty.size()),
        featureRate,
        40.0,
        240.0,
        120.0, // lastBpm to help bias towards correct octave
        0.0
    );

    REQUIRE(result.bpm > 175.0);
    REQUIRE(result.bpm < 185.0);
}

TEST_CASE("TempoEstimation: silence produces weak result", "[tempo]") {
    double featureRate = 93.75;
    std::vector<float> silence(500, 0.0f);

    TempoEstimation te(500);
    auto result = te.estimate(
        silence.data(),
        500,
        featureRate,
        40.0,
        240.0,
        0.0,
        0.0
    );

    REQUIRE(result.strength < 0.1);
}

TEST_CASE("TempoEstimation: Bounding constraints enforces minBpm and maxBpm", "[tempo]") {
    double featureRate = 93.75; 
    // Signal is 120 BPM
    auto novelty = generatePeriodicNovelty(120.0, featureRate, 5.0);

    TempoEstimation te(static_cast<int>(novelty.size()));
    
    // Limit tempo search to [160.0, 200.0], excluding 120 BPM
    auto result = te.estimate(
        novelty.data(),
        static_cast<int>(novelty.size()),
        featureRate,
        160.0, 
        200.0,
        0.0,
        0.0
    );

    // estimated BPM must stay within defined boundary despite the 120 BPM signal
    REQUIRE(result.bpm >= 160.0);
    REQUIRE(result.bpm <= 200.0);
}

TEST_CASE("TempoEstimation: estimate returns empty result if length is less than 2", "[tempo]") {
    TempoEstimation te(1024);
    float singleSample = 1.0f;

    auto result = te.estimate(&singleSample, 1, 93.75, 40.0, 240.0, 0.0, 0.0);

    // Aborted estimation should return default TempoResult (0.0 BPM)
    REQUIRE(result.bpm == 0.0);
    REQUIRE(result.strength == 0.0);
}

TEST_CASE("TempoEstimation: degenerate BPM window stays in bounds", "[tempo]") {
    // Regression test: a collapsed search window (e.g. the dynamic window shrinking
    // around a degenerate stable BPM) used to leave bestLag beyond the analysed
    // buffer, making applyBreakthroughLogic read autocorr out of range (SIGSEGV on
    // memory-tagged Android devices, garbage estimates elsewhere).
    double featureRate = 93.75;
    auto novelty = generatePeriodicNovelty(120.0, featureRate, 4.0);

    TempoEstimation te(static_cast<int>(novelty.size()));

    // maxBpm of 2 -> minLag ~2812, far beyond the ~375-sample buffer.
    auto result = te.estimate(
        novelty.data(),
        static_cast<int>(novelty.size()),
        featureRate,
        1.0,
        2.0,
        0.0,
        0.0
    );

    // Must not crash, and must return a finite, non-negative tempo.
    REQUIRE(std::isfinite(result.bpm));
    REQUIRE(result.bpm >= 0.0);
}