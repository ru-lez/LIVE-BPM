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

    // Update all non-structural tunable parameters and reset the engine.
    // Structural fields (sampleRate, blockSize, fftOrder, analysisWindowSeconds)
    // are ignored — those can only be set at construction time.
    void updateTunableConfig(const Config& newConfig);

    double getVisualDeadzone() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
    Result latestResult_;
};

} // namespace livebpm