#pragma once

#include <cstdint>

namespace livebpm {

struct Config {
    double sampleRate = 48000.0;
    int blockSize = 512;
    int fftOrder = 11; // 2^11 = 2048
    double minBpm = 40.0;
    double maxBpm = 240.0;
    double analysisWindowSeconds = 6.0;
};

} // namespace livebpm