#include <catch2/catch_test_macros.hpp>
#include "livebpm/dsp/BeatTrackingDP.h"
#include "TestSignals.h"

using namespace livebpm::dsp;
using namespace livebpm::test;

TEST_CASE("BeatTrackingDP: stub returns empty result", "[dp]") {
    BeatTrackingDP dp;
    std::vector<float> novelty = {0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
    
    auto result = dp.track(novelty.data(), 
                           static_cast<int>(novelty.size()), 
                           2);
                           
    // The stub returns empty. This test will pass now and will serve
    // as scaffolding when you implement the actual logic.
    REQUIRE(result.beatIndices.empty());
}

TEST_CASE("TestSignals: generatePeriodicNovelty works", "[helpers]") {
    auto novelty = generatePeriodicNovelty(120.0, 100.0, 5.0);
    
    REQUIRE(novelty.size() == 500);
    
    // At 120 BPM, feature_rate 100: beat every 50 frames
    // There should be a '1' at positions 0, 50, 100, 150, ...
    REQUIRE(novelty[0] == 1.0f);
    REQUIRE(novelty[50] == 1.0f);
    REQUIRE(novelty[100] == 1.0f);
    REQUIRE(novelty[25] == 0.0f); // between beats = 0
}