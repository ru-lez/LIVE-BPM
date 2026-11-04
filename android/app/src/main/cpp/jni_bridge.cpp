// JNI bridge for the LiveBPM engine — the Android analogue of livebpm/wasm/bridge.cpp.
//
// It owns a livebpm::Engine and exposes a thin C ABI that Kotlin's NativeBridge
// calls into. A pointer to BridgeState is handed to Kotlin as an opaque jlong
// handle. Like the web AudioWorklet, raw microphone samples are fed straight to
// the engine (no pre-filtering), so detection matches the web build exactly.

#include <jni.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "livebpm/Engine.h"
#include "livebpm/Presets.h"
#include "livebpm/dsp/Biquad.h"

using namespace livebpm;

namespace {

struct BridgeState {
    std::unique_ptr<Engine> engine;
    // Serialises engine access: audio thread (processBlock) vs UI thread
    // (setters / preset loads / getResult). The web build gets this for free
    // because everything runs on the single AudioWorklet thread.
    std::mutex mutex;
    // 80 Hz high-pass, like the CLI's live-mic path (cli/main.cpp): removes the
    // DC offset and handling rumble that phone mics deliver on the UNPROCESSED
    // source. Browsers already hand the web build an AC-coupled signal, so this
    // is what makes the engine hear equivalent input on Android.
    dsp::Biquad highpass;
    std::vector<float> filtered;
    double sampleRate = 48000.0;
    bool filterInitialized = false;
};

inline BridgeState* asState(jlong handle) {
    return reinterpret_cast<BridgeState*>(handle);
}

NoteResolution intToResolution(jint v) {
    switch (v) {
        case 1:  return NoteResolution::HalfNote;
        case 2:  return NoteResolution::QuarterNote;
        case 3:  return NoteResolution::EighthNote;
        case 4:  return NoteResolution::EighthNoteTriplet;
        case 5:  return NoteResolution::SixteenthNote;
        case 6:  return NoteResolution::SmartMixed;
        default: return NoteResolution::QuarterNote;
    }
}

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_livebpm_android_NativeBridge_nativeCreate(
        JNIEnv*, jobject, jdouble sampleRate, jint blockSize) {
    auto* state = new BridgeState();
    Config config = GetPreset("default");
    config.sampleRate = sampleRate;
    config.blockSize = blockSize;
    state->engine = std::make_unique<Engine>(config);
    state->sampleRate = sampleRate;
    return reinterpret_cast<jlong>(state);
}

JNIEXPORT void JNICALL
Java_com_livebpm_android_NativeBridge_nativeDestroy(JNIEnv*, jobject, jlong handle) {
    delete asState(handle);
}

JNIEXPORT void JNICALL
Java_com_livebpm_android_NativeBridge_nativeReset(JNIEnv*, jobject, jlong handle) {
    auto* s = asState(handle);
    if (!s || !s->engine) return;
    std::lock_guard<std::mutex> lock(s->mutex);
    s->engine->reset();
    s->highpass.reset();
}

JNIEXPORT void JNICALL
Java_com_livebpm_android_NativeBridge_nativeProcess(
        JNIEnv* env, jobject, jlong handle, jfloatArray samples, jint numSamples) {
    auto* s = asState(handle);
    if (!s || !s->engine || samples == nullptr || numSamples <= 0) return;

    jfloat* raw = env->GetFloatArrayElements(samples, nullptr);
    if (raw == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(s->mutex);

        // Initialise the high-pass on first use (see BridgeState::highpass).
        if (!s->filterInitialized) {
            s->highpass.setCoefficients(dsp::Biquad::makeHighpass(s->sampleRate, 80.0));
            s->filterInitialized = true;
        }
        if (s->filtered.size() < static_cast<size_t>(numSamples)) {
            s->filtered.resize(static_cast<size_t>(numSamples));
        }
        for (jint i = 0; i < numSamples; ++i) {
            s->filtered[static_cast<size_t>(i)] = s->highpass.process(raw[i]);
        }

        try {
            s->engine->processBlock(s->filtered.data(), numSamples);
        } catch (...) {
            // A C++ exception must never cross the JNI boundary (it aborts the
            // process). Recover by flushing the engine's DSP state instead.
            try { s->engine->reset(); } catch (...) {}
        }
    }
    // Read-only access — abort copy-back to avoid mutating the Java array.
    env->ReleaseFloatArrayElements(samples, raw, JNI_ABORT);
}

JNIEXPORT jdoubleArray JNICALL
Java_com_livebpm_android_NativeBridge_nativeGetResult(JNIEnv* env, jobject, jlong handle) {
    auto* s = asState(handle);
    jdouble out[4] = {0.0, 0.0, 0.0, 0.0};
    if (s && s->engine) {
        std::lock_guard<std::mutex> lock(s->mutex);
        Result r = s->engine->getLatestResult();
        out[0] = r.bpm;
        out[1] = r.confidence;
        out[2] = r.hasValidEstimate ? 1.0 : 0.0;
        out[3] = r.processingTimeMs;
    }
    jdoubleArray arr = env->NewDoubleArray(4);
    env->SetDoubleArrayRegion(arr, 0, 4, out);
    return arr;
}

JNIEXPORT jdoubleArray JNICALL
Java_com_livebpm_android_NativeBridge_nativeLoadPreset(
        JNIEnv* env, jobject, jlong handle, jstring name) {
    auto* s = asState(handle);

    const char* cname = env->GetStringUTFChars(name, nullptr);
    Config c = GetPreset(cname ? std::string(cname) : std::string("default"));
    if (cname) env->ReleaseStringUTFChars(name, cname);

    if (s && s->engine) {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->engine->updateTunableConfig(c);
    }

    // Order MUST match PRESET_PARAM_ORDER in Params.kt (indices 0..23);
    // index 24 carries useHeuristics as 1.0 / 0.0.
    jdouble out[25] = {
        c.onsetSensitivity,                  // 0
        c.complexDomainWeight,               // 1
        c.fluxCompression,                   // 2
        c.noveltySmoothing,                  // 3
        c.SILENCE_THRESHOLD,                 // 4  (silenceThreshold)
        c.gatingAlpha,                       // 5
        c.silenceTimeoutSec,                 // 6
        c.dynamicWindowPercent,              // 7
        c.minBpm,                            // 8
        c.maxBpm,                            // 9
        c.hysteresisBlend,                   // 10
        static_cast<double>(c.octaveMismatchTolerance), // 11
        c.fastTempoThreshold,                // 12
        c.doubleTimeJumpThreshold,           // 13
        c.halfTimeJumpThreshold,             // 14
        c.hardResetThreshold,                // 15
        static_cast<double>(c.warmupHops),   // 16
        c.lockEntryConfidence,               // 17
        c.lockReleaseConfidence,             // 18
        c.lockedDriftTolerance,              // 19
        c.uiSmoothing,                       // 20
        c.VISUAL_DEADZONE,                   // 21 (visualDeadzone)
        c.magneticSnapRange,                 // 22
        c.confThreshold,                     // 23
        c.useHeuristics ? 1.0 : 0.0          // 24
    };

    jdoubleArray arr = env->NewDoubleArray(25);
    env->SetDoubleArrayRegion(arr, 0, 25, out);
    return arr;
}

JNIEXPORT void JNICALL
Java_com_livebpm_android_NativeBridge_nativeSetResolution(
        JNIEnv*, jobject, jlong handle, jint res) {
    auto* s = asState(handle);
    if (s && s->engine) {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->engine->setTargetResolution(intToResolution(res));
    }
}

JNIEXPORT void JNICALL
Java_com_livebpm_android_NativeBridge_nativeSetHeuristics(
        JNIEnv*, jobject, jlong handle, jboolean enabled) {
    auto* s = asState(handle);
    if (s && s->engine) {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->engine->setHeuristicsEnabled(enabled == JNI_TRUE);
    }
}

JNIEXPORT void JNICALL
Java_com_livebpm_android_NativeBridge_nativeSetParam(
        JNIEnv* env, jobject, jlong handle, jstring param, jdouble value) {
    auto* s = asState(handle);
    if (!s || !s->engine || param == nullptr) return;

    const char* p = env->GetStringUTFChars(param, nullptr);
    if (!p) return;
    std::string name(p);
    env->ReleaseStringUTFChars(param, p);

    std::lock_guard<std::mutex> lock(s->mutex);
    Engine& e = *s->engine;
    if      (name == "onsetSensitivity")        e.setOnsetSensitivity(value);
    else if (name == "complexDomainWeight")     e.setComplexDomainWeight(value);
    else if (name == "fluxCompression")         e.setFluxCompression(value);
    else if (name == "noveltySmoothing")        e.setNoveltySmoothing(value);
    else if (name == "silenceThreshold")        e.setSilenceThreshold(value);
    else if (name == "gatingAlpha")             e.setGatingAlpha(value);
    else if (name == "silenceTimeoutSec")       e.setSilenceTimeoutSec(value);
    else if (name == "dynamicWindowPercent")    e.setDynamicWindowPercent(value);
    else if (name == "minBpm")                  e.setMinBpm(value);
    else if (name == "maxBpm")                  e.setMaxBpm(value);
    else if (name == "hysteresisBlend")         e.setHysteresisBlend(value);
    else if (name == "octaveMismatchTolerance") e.setOctaveMismatchTolerance(static_cast<int>(value));
    else if (name == "fastTempoThreshold")      e.setFastTempoThreshold(value);
    else if (name == "doubleTimeJumpThreshold") e.setDoubleTimeJumpThreshold(value);
    else if (name == "halfTimeJumpThreshold")   e.setHalfTimeJumpThreshold(value);
    else if (name == "hardResetThreshold")      e.setHardResetThreshold(value);
    else if (name == "warmupHops")              e.setWarmupHops(static_cast<int>(value));
    else if (name == "lockEntryConfidence")     e.setLockEntryConfidence(value);
    else if (name == "lockReleaseConfidence")   e.setLockReleaseConfidence(value);
    else if (name == "lockedDriftTolerance")    e.setLockedDriftTolerance(value);
    else if (name == "uiSmoothing")             e.setUiSmoothing(value);
    else if (name == "visualDeadzone")          e.setVisualDeadzone(value);
    else if (name == "magneticSnapRange")       e.setMagneticSnapRange(value);
    else if (name == "confThreshold")           e.setConfThreshold(value);
}

} // extern "C"
