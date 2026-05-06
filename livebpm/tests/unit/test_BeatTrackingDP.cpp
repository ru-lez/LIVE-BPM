#include <catch2/catch_test_macros.hpp>
#include "livebpm/dsp/BeatTrackingDP.h"
#include "TestSignals.h"

using namespace livebpm::dsp;
using namespace livebpm::test;

TEST_CASE("BeatTrackingDP: periodic novelty produces regular beats", "[dp]") {
    double featureRate = 93.75;
    auto novelty = generatePeriodicNovelty(120.0, featureRate, 5.0);
    int beatRefLag = static_cast<int>(60.0 / 120.0 * featureRate);
    // ~47

    BeatTrackingDP dp;
    auto result = dp.track(
        novelty.data(),
        static_cast<int>(novelty.size()),
        beatRefLag
    );

    // Should find beats
    REQUIRE(result.beatIndices.size() >= 5);

    // Beats should be approximately separated by beatRefLag
    for (size_t i = 1; i < result.beatIndices.size(); ++i) {
        int interval = result.beatIndices[i] - result.beatIndices[i - 1];
        REQUIRE(interval > beatRefLag / 2);
        REQUIRE(interval < beatRefLag * 2);
    }

    // High stability for a perfectly periodic signal
    REQUIRE(result.stability > 0.7);
}

TEST_CASE("BeatTrackingDP: beats align with novelty peaks", "[dp]") {
    // Novelty with clear peaks at known positions
    std::vector<float> novelty(200, 0.0f);
    novelty[20] = 1.0f;
    novelty[70] = 1.0f;
    novelty[120] = 1.0f;
    novelty[170] = 1.0f;

    BeatTrackingDP dp;
    auto result = dp.track(novelty.data(), 200, 50);

    // Alignment should be high (beats coincide with peaks)
    REQUIRE(result.alignment > 0.5);
}

TEST_CASE("BeatTrackingDP: empty novelty returns empty result", "[dp]") {
    BeatTrackingDP dp;
    auto result = dp.track(nullptr, 0, 50);
    REQUIRE(result.beatIndices.empty());
}

TEST_CASE("BeatTrackingDP: single sample doesn't crash", "[dp]") {
    float single = 1.0f;
    BeatTrackingDP dp;
    auto result = dp.track(&single, 1, 50);

    // Not crashing is the success here
}

TEST_CASE("BeatTrackingDP: Coverage metric calculates correct fraction", "[dp]") {
    double featureRate = 93.75;
    // Generate 10 seconds of consistent 120 BPM novelty
    auto novelty = generatePeriodicNovelty(120.0, featureRate, 10.0);
    int beatRefLag = static_cast<int>(60.0 / 120.0 * featureRate);

    BeatTrackingDP dp;
    auto result = dp.track(novelty.data(), static_cast<int>(novelty.size()), beatRefLag);

    // novelty function generates peaks consistently across the entire 10 seconds
    // the span of beats should cover nearly the entire window length (> 95%)
    REQUIRE(result.coverage > 0.95);
}

TEST_CASE("BeatTrackingDP: Penalty factor adjustment processes without errors", "[dp]") {
    double featureRate = 93.75;
    auto novelty = generatePeriodicNovelty(120.0, featureRate, 5.0);
    int beatRefLag = static_cast<int>(60.0 / 120.0 * featureRate);

    BeatTrackingDP dp;
    
    // Test with standard penalty factor
    auto defaultResult = dp.track(novelty.data(), static_cast<int>(novelty.size()), beatRefLag, 1.0);
    
    // Test with extremely high penalty factor to enforce rigid spacing
    auto rigidResult = dp.track(novelty.data(), static_cast<int>(novelty.size()), beatRefLag, 100.0);

    REQUIRE(!defaultResult.beatIndices.empty());
    REQUIRE(!rigidResult.beatIndices.empty());
}