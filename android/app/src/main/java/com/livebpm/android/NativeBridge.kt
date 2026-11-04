package com.livebpm.android

/**
 * Kotlin <-> C++ bridge for the LiveBPM engine (liblivebpm.so).
 *
 * The Android analogue of the WASM Embind wrapper in livebpm/wasm/bridge.cpp:
 * every call operates on an opaque [Long] handle returned by [nativeCreate].
 * Implementations live in app/src/main/cpp/jni_bridge.cpp.
 */
object NativeBridge {

    init {
        System.loadLibrary("livebpm")
    }

    /** Create an engine; returns an opaque handle (0 on failure). */
    external fun nativeCreate(sampleRate: Double, blockSize: Int): Long

    /** Destroy an engine created by [nativeCreate]. */
    external fun nativeDestroy(handle: Long)

    /** Flush all internal DSP state (mirrors the web "Flush" button). */
    external fun nativeReset(handle: Long)

    /** Feed a block of mono float samples (high-passed natively before analysis). */
    external fun nativeProcess(handle: Long, samples: FloatArray, numSamples: Int)

    /** Latest result as [bpm, confidence, hasValidEstimate(0/1), processingTimeMs]. */
    external fun nativeGetResult(handle: Long): DoubleArray

    /**
     * Apply an instrument preset and return its tunable values, ordered to match
     * [Params.PRESET_PARAM_ORDER] (indices 0..23) with index 24 = useHeuristics.
     */
    external fun nativeLoadPreset(handle: Long, name: String): DoubleArray

    /** Set the target note resolution (1=1/2, 2=1/4, 3=1/8, 4=triplet, 5=1/16, 6=smart). */
    external fun nativeSetResolution(handle: Long, res: Int)

    /** Enable/disable octave & pattern correction heuristics. */
    external fun nativeSetHeuristics(handle: Long, enabled: Boolean)

    /** Set a single tunable parameter by id (see [Params.sliderParams]). */
    external fun nativeSetParam(handle: Long, param: String, value: Double)
}
