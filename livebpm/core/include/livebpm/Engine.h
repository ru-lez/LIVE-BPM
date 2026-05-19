#pragma once

#include "livebpm/Config.h"
#include "livebpm/Result.h"
#include <memory>

namespace livebpm {

class Engine {
public:
    explicit Engine(const Config& config);
    ~Engine();

    void processBlock(const float* samples, int numSamples);
    Result getLatestResult() const;
    void reset();

    // Advanced Mode
    void setTargetResolution(NoteResolution resolution);
    void setHeuristicsEnabled(bool setting);
    void setOnsetSensitivity(double sensitivity);
    void setComplexDomainWeight(double weight);
    void setSilenceThreshold(double threshold);
    void setMinBpm(double minBpm);
    void setMaxBpm(double maxBpm);
    void setDoubleTimeJumpThreshold(double threshold);
    void setHalfTimeJumpThreshold(double threshold);

    double getVisualDeadzone() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
    Result latestResult_;
};

} // namespace livebpm