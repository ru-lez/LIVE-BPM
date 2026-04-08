#include <catch2/catch_test_macros.hpp>
#include "livebpm/Engine.h"
#include "TestSignals.h"
using namespace livebpm;
using namespace livebpm::test;
TEST_CASE("Full pipeline: 120 BPM click track", "[integration]") {
 Config config;
 config.sampleRate = 48000;
 config.blockSize = 512;
 Engine engine(config);
 // Generate 10 seconds of click track at 120 BPM
 auto signal = generateClickTrack(120.0, config.sampleRate, 10.0);
 // Feed block by block
 for (size_t offset = 0; offset + config.blockSize <=
signal.size();
 offset += config.blockSize) {
 engine.processBlock(&signal[offset], config.blockSize);
 }
 auto result = engine.getLatestResult();
 // Should have a valid BPM
 REQUIRE(result.hasValidEstimate);
 // BPM should be close to 120
 REQUIRE(result.bpm > 110.0);
 REQUIRE(result.bpm < 130.0);
 // Reasonable confidence
 REQUIRE(result.confidence > 0.3);
}