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

    void setTargetResolution(NoteResolution resolution);
    void setHeuristicsEnabled(bool setting);

    double getVisualDeadzone() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
    Result latestResult_;
};

} // namespace livebpm