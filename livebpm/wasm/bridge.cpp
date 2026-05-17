#include <emscripten/bind.h>
#include "livebpm/Engine.h"

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

    void setTargetResolution(livebpm::NoteResolution res) {
        engine_->setTargetResolution(res); 
    }

    void reset() {
        engine_->reset();
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
        .function("reset", &LiveBpmWasm::reset);
}