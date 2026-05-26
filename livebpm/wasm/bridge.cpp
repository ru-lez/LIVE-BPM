// Ignoring third-party libraries for -Wall -Wextra -Wpedantic
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <emscripten/bind.h>
#include <emscripten/val.h>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "livebpm/Engine.h"
#include "livebpm/Presets.h"

using namespace emscripten;

// Lightweight wrapper that simplifies the API for JS
class LiveBpmWasm {
public:
    LiveBpmWasm(double sampleRate, int blockSize) {
        livebpm::Config config;
        config.sampleRate = sampleRate;
        config.blockSize = blockSize;
        engine_ = std::make_unique<livebpm::Engine>(config);
    }

    // Processes audio from a pointer in the WASM heap
    void processBlock(uintptr_t samplesPtr, int numSamples) {
        const float* samples = reinterpret_cast<const float*>(samplesPtr);
        engine_->processBlock(samples, numSamples);
    }

    // Returns current BPM
    double getBpm() {
        return engine_->getLatestResult().bpm;
    }

    double getVisualDeadzone() {
        return engine_->getVisualDeadzone();
    }

    // Returns confidence [0, 1]
    double getConfidence() {
        return engine_->getLatestResult().confidence;
    }

    // Has a valid estimate?
    bool hasValidEstimate() {
        return engine_->getLatestResult().hasValidEstimate;
    }

    // Returns full result as a JS object
    val getResult() {
        auto r = engine_->getLatestResult();
        val result = val::object();
        result.set("bpm", r.bpm);
        result.set("confidence", r.confidence);
        result.set("hasValidEstimate", r.hasValidEstimate);
        result.set("processingTimeMs", r.processingTimeMs);
        return result;
    }
    
    val loadPreset(std::string name) {
        livebpm::Config c = livebpm::GetPreset(name);
        
        // Pass the config to the engine
        engine_->updateTunableConfig(c);

        val result = val::object();
        result.set("useHeuristics", c.useHeuristics);
        result.set("silenceThreshold", c.SILENCE_THRESHOLD);
        result.set("onsetSensitivity", c.onsetSensitivity);
        result.set("complexDomainWeight", c.complexDomainWeight);
        result.set("uiSmoothing", c.uiSmoothing);
        result.set("visualDeadzone", c.VISUAL_DEADZONE);
        result.set("minBpm", c.minBpm);
        result.set("maxBpm", c.maxBpm);
        result.set("doubleTimeJumpThreshold", c.doubleTimeJumpThreshold);
        result.set("halfTimeJumpThreshold", c.halfTimeJumpThreshold);
        result.set("gatingAlpha", c.gatingAlpha);
        result.set("noveltySmoothing", c.noveltySmoothing);
        result.set("fluxCompression", c.fluxCompression);
        result.set("hysteresisBlend", c.hysteresisBlend);
        result.set("magneticSnapRange", c.magneticSnapRange);
        result.set("fastTempoThreshold", c.fastTempoThreshold);
        result.set("silenceTimeoutSec", c.silenceTimeoutSec);
        result.set("warmupHops", c.warmupHops);
        result.set("confThreshold", c.confThreshold);
        result.set("octaveMismatchTolerance", c.octaveMismatchTolerance);
        result.set("hardResetThreshold", c.hardResetThreshold);
        
        return result;
    }

    void setTargetResolution(livebpm::NoteResolution res) {
        engine_->setTargetResolution(res); 
    }

    void reset() {
        engine_->reset();
    }

    void setUseHeuristics(bool setting) {
        engine_->setHeuristicsEnabled(setting);
    }
    void setOnsetSensitivity(double val) { 
        engine_->setOnsetSensitivity(val);
    }
    void setComplexDomainWeight(double val) {
        engine_->setComplexDomainWeight(val);
    }
    void setSilenceThreshold(double val) {
        engine_->setSilenceThreshold(val);
    }
    void setMinBpm(double val) { 
        engine_->setMinBpm(val); 
    }
    void setMaxBpm(double val) { 
        engine_->setMaxBpm(val); 
    }
    void setDoubleTimeJumpThreshold(double val) { 
        engine_->setDoubleTimeJumpThreshold(val); 
    }
    void setHalfTimeJumpThreshold(double val) { 
        engine_->setHalfTimeJumpThreshold(val); 
    }
    void setGatingAlpha(double val) { 
        engine_->setGatingAlpha(val); 
    }
    void setNoveltySmoothing(double val) { 
        engine_->setNoveltySmoothing(val); 
    }
    void setFluxCompression(double val) { 
        engine_->setFluxCompression(val); 
    }
    void setHysteresisBlend(double val) { 
        engine_->setHysteresisBlend(val); 
    }
    void setMagneticSnapRange(double val) { 
        engine_->setMagneticSnapRange(val); 
    }
    void setFastTempoThreshold(double value) {
        engine_->setFastTempoThreshold(value);
    }
    void setSilenceTimeoutSec(double value) {
        engine_->setSilenceTimeoutSec(value);
    }
    void setWarmupHops(int value) {
        engine_->setWarmupHops(value);
    }
    void setUiSmoothing(double alpha) {
        engine_->setUiSmoothing(alpha);
    }
    void setVisualDeadzone(double deadzone) {
        engine_->setVisualDeadzone(deadzone);
    }
    void setConfThreshold(double val) {
        engine_->setConfThreshold(val);
    }
    void setOctaveMismatchTolerance(int val) {
        engine_->setOctaveMismatchTolerance(val);
    }
    void setHardResetThreshold(double val) {
        engine_->setHardResetThreshold(val);
    }

private:
    std::unique_ptr<livebpm::Engine> engine_;
};

// Register the class with Embind
EMSCRIPTEN_BINDINGS(livebpm) {
    enum_<livebpm::NoteResolution>("NoteResolution")
        .value("HalfNote", livebpm::NoteResolution::HalfNote)
        .value("QuarterNote", livebpm::NoteResolution::QuarterNote)
        .value("EighthNote", livebpm::NoteResolution::EighthNote)
        .value("EighthNoteTriplet", livebpm::NoteResolution::EighthNoteTriplet)
        .value("SixteenthNote", livebpm::NoteResolution::SixteenthNote)
        .value("SmartMixed", livebpm::NoteResolution::SmartMixed);
        
    class_<LiveBpmWasm>("LiveBpmEngine")
        .constructor<double, int>()
        .function("processBlock", &LiveBpmWasm::processBlock)
        .function("getBpm", &LiveBpmWasm::getBpm)
        .function("getConfidence", &LiveBpmWasm::getConfidence)
        .function("hasValidEstimate", &LiveBpmWasm::hasValidEstimate)
        .function("getResult", &LiveBpmWasm::getResult)
        .function("setTargetResolution", &LiveBpmWasm::setTargetResolution)
        .function("getVisualDeadzone", &LiveBpmWasm::getVisualDeadzone)
        .function("reset", &LiveBpmWasm::reset)

        // Advanced Mode
        .function("setUseHeuristics", &LiveBpmWasm::setUseHeuristics)
        .function("setOnsetSensitivity", &LiveBpmWasm::setOnsetSensitivity)
        .function("setComplexDomainWeight", &LiveBpmWasm::setComplexDomainWeight)
        .function("setSilenceThreshold", &LiveBpmWasm::setSilenceThreshold)
        .function("setMinBpm", &LiveBpmWasm::setMinBpm)
        .function("setMaxBpm", &LiveBpmWasm::setMaxBpm)
        .function("setDoubleTimeJumpThreshold", &LiveBpmWasm::setDoubleTimeJumpThreshold)
        .function("setHalfTimeJumpThreshold", &LiveBpmWasm::setHalfTimeJumpThreshold)
        .function("setGatingAlpha", &LiveBpmWasm::setGatingAlpha)
        .function("setNoveltySmoothing", &LiveBpmWasm::setNoveltySmoothing)
        .function("setFluxCompression", &LiveBpmWasm::setFluxCompression)
        .function("setHysteresisBlend", &LiveBpmWasm::setHysteresisBlend)
        .function("setMagneticSnapRange", &LiveBpmWasm::setMagneticSnapRange)
        .function("setFastTempoThreshold", &LiveBpmWasm::setFastTempoThreshold)
        .function("setSilenceTimeoutSec", &LiveBpmWasm::setSilenceTimeoutSec)
        .function("setWarmupHops", &LiveBpmWasm::setWarmupHops)
        .function("setUiSmoothing", &LiveBpmWasm::setUiSmoothing)
        .function("setVisualDeadzone", &LiveBpmWasm::setVisualDeadzone)
        .function("setConfThreshold", &LiveBpmWasm::setConfThreshold)
        .function("setOctaveMismatchTolerance", &LiveBpmWasm::setOctaveMismatchTolerance)
        .function("setHardResetThreshold", &LiveBpmWasm::setHardResetThreshold)
        .function("loadPreset", &LiveBpmWasm::loadPreset);
}