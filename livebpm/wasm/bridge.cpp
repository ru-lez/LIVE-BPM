#include <emscripten/bind.h>
#include "livebpm/Engine.h"
#include "livebpm/InstrumentPresets.h"

using namespace emscripten;

// Lightweight wrapper that simplifies the API for JS
class LiveBpmWasm {
public:
    LiveBpmWasm(double sampleRate, int blockSize) {
        config_.sampleRate = sampleRate;
        config_.blockSize  = blockSize;
        engine_ = std::make_unique<livebpm::Engine>(config_);
    }

    // Processes audio from a pointer in the WASM heap
    void processBlock(uintptr_t samplesPtr, int numSamples) {
        const float* samples = reinterpret_cast<const float*>(samplesPtr);
        engine_->processBlock(samples, numSamples);
    }

    double getBpm()            { return engine_->getLatestResult().bpm; }
    double getConfidence()     { return engine_->getLatestResult().confidence; }
    bool   hasValidEstimate()  { return engine_->getLatestResult().hasValidEstimate; }
    double getVisualDeadzone() { return engine_->getVisualDeadzone(); }

    val getResult() {
        auto r = engine_->getLatestResult();
        val result = val::object();
        result.set("bpm",              r.bpm);
        result.set("confidence",       r.confidence);
        result.set("hasValidEstimate", r.hasValidEstimate);
        result.set("processingTimeMs", r.processingTimeMs);
        return result;
    }

    void setTargetResolution(livebpm::NoteResolution res) {
        config_.targetResolution = res;
        engine_->setTargetResolution(res);
    }

    // Load a named instrument preset (id: 0=default, 1=flute, 2=guitar,
    // 3=saxophone, 4=acousticBass, 5=cajonFlamenco).
    // Structural fields (sampleRate, blockSize) and the current note resolution
    // are preserved across the preset switch.
    void setPreset(int id) {
        livebpm::Config preset;
        switch (id) {
            case 1:  preset = livebpm::presets::flute();         break;
            case 2:  preset = livebpm::presets::guitar();        break;
            case 3:  preset = livebpm::presets::saxophone();     break;
            case 4:  preset = livebpm::presets::acousticBass();  break;
            case 5:  preset = livebpm::presets::cajonFlamenco(); break;
            default: preset = livebpm::Config{};                 break;
        }
        preset.sampleRate       = config_.sampleRate;
        preset.blockSize        = config_.blockSize;
        preset.targetResolution = config_.targetResolution;
        config_ = preset;
        engine_->updateTunableConfig(config_);
    }

    // Set a single named parameter and immediately apply it to the engine.
    // Parameter names match the fields in livebpm::Config.
    void setParam(const std::string& name, double value) {
        if      (name == "semitoneFluxWeight")  config_.semitoneFluxWeight       = static_cast<float>(value);
        else if (name == "gatingOffset")        config_.gatingOffset             = static_cast<float>(value);
        else if (name == "gatingAlpha")         config_.gatingAlpha              = static_cast<float>(value);
        else if (name == "noveltySmoothing")    config_.noveltySmoothing         = static_cast<float>(value);
        else if (name == "fluxCompression")     config_.fluxCompression          = static_cast<float>(value);
        else if (name == "hysteresisBlend")     config_.hysteresisBlend          = static_cast<float>(value);
        else if (name == "minBpm")              config_.minBpm                   = value;
        else if (name == "maxBpm")              config_.maxBpm                   = value;
        else if (name == "silenceThreshold")    config_.SILENCE_THRESHOLD        = value;
        else if (name == "halfTimeThreshold")   config_.halfTimeJumpThreshold    = value;
        else if (name == "doubleTimeThreshold") config_.doubleTimeJumpThreshold  = value;
        engine_->updateTunableConfig(config_);
    }

    // Returns the current tunable config as a plain JS object.
    val getConfig() const {
        val obj = val::object();
        obj.set("semitoneFluxWeight",   static_cast<double>(config_.semitoneFluxWeight));
        obj.set("gatingOffset",         static_cast<double>(config_.gatingOffset));
        obj.set("gatingAlpha",          static_cast<double>(config_.gatingAlpha));
        obj.set("noveltySmoothing",     static_cast<double>(config_.noveltySmoothing));
        obj.set("fluxCompression",      static_cast<double>(config_.fluxCompression));
        obj.set("hysteresisBlend",      static_cast<double>(config_.hysteresisBlend));
        obj.set("minBpm",               config_.minBpm);
        obj.set("maxBpm",               config_.maxBpm);
        obj.set("silenceThreshold",     config_.SILENCE_THRESHOLD);
        obj.set("halfTimeThreshold",    config_.halfTimeJumpThreshold);
        obj.set("doubleTimeThreshold",  config_.doubleTimeJumpThreshold);
        return obj;
    }

    void reset() { engine_->reset(); }

private:
    std::unique_ptr<livebpm::Engine> engine_;
    livebpm::Config config_;
};

// Register the class with Embind
EMSCRIPTEN_BINDINGS(livebpm) {
    enum_<livebpm::NoteResolution>("NoteResolution")
        .value("HalfNote",          livebpm::NoteResolution::HalfNote)
        .value("QuarterNote",       livebpm::NoteResolution::QuarterNote)
        .value("EighthNote",        livebpm::NoteResolution::EighthNote)
        .value("EighthNoteTriplet", livebpm::NoteResolution::EighthNoteTriplet)
        .value("SixteenthNote",     livebpm::NoteResolution::SixteenthNote)
        .value("SmartMixed",        livebpm::NoteResolution::SmartMixed);

    class_<LiveBpmWasm>("LiveBpmEngine")
        .constructor<double, int>()
        .function("processBlock",        &LiveBpmWasm::processBlock)
        .function("getBpm",              &LiveBpmWasm::getBpm)
        .function("getConfidence",       &LiveBpmWasm::getConfidence)
        .function("hasValidEstimate",    &LiveBpmWasm::hasValidEstimate)
        .function("getResult",           &LiveBpmWasm::getResult)
        .function("setTargetResolution", &LiveBpmWasm::setTargetResolution)
        .function("getVisualDeadzone",   &LiveBpmWasm::getVisualDeadzone)
        .function("setPreset",           &LiveBpmWasm::setPreset)
        .function("setParam",            &LiveBpmWasm::setParam)
        .function("getConfig",           &LiveBpmWasm::getConfig)
        .function("reset",               &LiveBpmWasm::reset);
}
