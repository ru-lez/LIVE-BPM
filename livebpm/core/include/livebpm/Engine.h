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

    void updateTunableConfig(const Config& config);

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
    void setGatingAlpha(double alpha);
    void setNoveltySmoothing(double smoothing);
    void setFluxCompression(double compression);
    void setHysteresisBlend(double blend);
    void setMagneticSnapRange(double range);

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