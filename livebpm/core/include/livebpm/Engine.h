#pragma once
#include "livebpm/Config.h"
#include "livebpm/Result.h"
namespace livebpm {
class Engine {
public:
 explicit Engine(const Config& config);
 ~Engine();
 void processBlock(const float* samples, int numSamples);
 Result getLatestResult() const;
 void reset();
private:
 Config config_;
 Result latestResult_;
 // TODO: add DSP modules in Week 2-3
};
} // namespace livebpm