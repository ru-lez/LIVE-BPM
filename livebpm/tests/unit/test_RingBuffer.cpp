#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "livebpm/dsp/RingBuffer.h"
#include "TestSignals.h"

using namespace livebpm::dsp;
using namespace livebpm::test;

TEST_CASE("RingBuffer: clear resets state and data", "[ringbuffer]") {
    RingBuffer<float> rb(1024);
    std::vector<float> data = {1.0f, 2.0f, 3.0f};
    rb.write(data.data(), 3);
    
    // Perform clear
    rb.clear();
    
    // The available count should return to 0
    REQUIRE(rb.available() == 0);
    REQUIRE(rb.capacity() == 1024);
}

TEST_CASE("RingBuffer: write handles overflow exceeding capacity in a single pass", "[ringbuffer]") {
    RingBuffer<float> rb(3); // Small capacity of 3
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f}; // 5 samples
    
    // Write 5 samples into a capacity of 3. 
    // Should discard 1.0 and 2.0, keeping only the last 3 (3.0, 4.0, 5.0).
    rb.write(data.data(), 5);
    
    REQUIRE(rb.available() == 3);
    
    std::vector<float> outData(3, 0.0f);
    rb.readLatest(outData.data(), 3);
    
    REQUIRE_THAT(outData[0], Catch::Matchers::WithinAbs(3.0, 1e-5));
    REQUIRE_THAT(outData[1], Catch::Matchers::WithinAbs(4.0, 1e-5));
    REQUIRE_THAT(outData[2], Catch::Matchers::WithinAbs(5.0, 1e-5));
}

TEST_CASE("RingBuffer: readLatest handles requesting more than available", "[ringbuffer]") {
    RingBuffer<float> rb(10);
    std::vector<float> data = {1.0f, 2.0f};
    
    // 2 samples available
    rb.write(data.data(), 2); 
    
    std::vector<float> outData(5, 0.0f); 
    
    // Requesting 5, but only 2 available
    rb.readLatest(outData.data(), 5);
    
    // Should only write the 2 available samples to the start of the output buffer
    REQUIRE_THAT(outData[0], Catch::Matchers::WithinAbs(1.0, 1e-5));
    REQUIRE_THAT(outData[1], Catch::Matchers::WithinAbs(2.0, 1e-5));
    
    // Rest of pre-allocated outData should not change (0.0f)
    REQUIRE_THAT(outData[2], Catch::Matchers::WithinAbs(0.0, 1e-5));
}