#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "livebpm/dsp/SpectralAnalysis.h"

using namespace livebpm::dsp;

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