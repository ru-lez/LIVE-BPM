package com.livebpm.android

import java.util.Locale

/** One Advanced-menu slider, transcribed from livebpm/wasm/index.html. */
data class SliderParam(
    val id: String,
    val label: String,
    val tip: String,
    val min: Float,
    val max: Float,
    val step: Float,
    val decimals: Int,
    val default: Float,
)

/** A group of sliders rendered under a heading (mirrors the web `.param-section`s). */
data class ParamSection(
    val title: String,
    val params: List<SliderParam>,
)

/** Note-resolution radio option. */
data class ResolutionOption(val value: Int, val label: String, val tip: String)

/** Instrument preset (dropdown value -> display label). */
data class PresetOption(val value: String, val label: String)

object Params {

    val sections: List<ParamSection> = listOf(
        ParamSection(
            "Onset Detection",
            listOf(
                SliderParam(
                    "onsetSensitivity", "Onset Sensitivity",
                    "Determines the minimum energy jump required to trigger a note. Lower values catch quiet taps but may increase false positives.",
                    0.005f, 0.1f, 0.005f, 3, 0.015f
                ),
                SliderParam(
                    "complexDomainWeight", "Complex Domain Weight",
                    "Balances detection methods. Higher values track smooth legato instruments (like flutes) better, lower values favour punchy percussive hits.",
                    0.0f, 1.0f, 0.05f, 2, 0.4f
                ),
                SliderParam(
                    "fluxCompression", "Flux Compression (Log1p)",
                    "Flattens extreme volume differences, ensuring quiet notes and heavy accents are weighted more equally by the tracking engine.",
                    10.0f, 100.0f, 1.0f, 0, 50.0f
                ),
                SliderParam(
                    "noveltySmoothing", "Novelty Smoothing",
                    "Applies a moving average to the raw audio energy stream. Higher values reduce beat jitter but slightly delay the engine's reaction time.",
                    0.05f, 0.9f, 0.05f, 2, 0.4f
                ),
            )
        ),
        ParamSection(
            "Noise & Adaptive Gating",
            listOf(
                SliderParam(
                    "silenceThreshold", "Noise Gate (Silence)",
                    "Sets the absolute volume floor. Increase this to stop background room noise or microphone hum from triggering false beats.",
                    0.0f, 0.1f, 0.005f, 3, 0.03f
                ),
                SliderParam(
                    "gatingAlpha", "Gating Alpha (Adaptation)",
                    "Controls how fast the internal noise gate adapts to the musician naturally getting louder or softer over time.",
                    0.01f, 0.5f, 0.01f, 2, 0.1f
                ),
                SliderParam(
                    "silenceTimeoutSec", "Silence Timeout (sec)",
                    "Seconds of silence required before the engine flushes its internal state.",
                    0.5f, 5.0f, 0.5f, 1, 2.0f
                ),
            )
        ),
        ParamSection(
            "Tempo Tracking & Locking",
            listOf(
                SliderParam(
                    "dynamicWindowPercent", "Dynamic Window Size",
                    "Dynamically restricts the BPM search boundary around the current stable tempo to prevent double/half-time errors.",
                    0.10f, 1.00f, 0.05f, 2, 0.40f
                ),
                SliderParam(
                    "minBpm", "Target Minimum BPM",
                    "The lowest tempo range the engine will initially search for.",
                    30.0f, 120.0f, 1.0f, 0, 40.0f
                ),
                SliderParam(
                    "maxBpm", "Target Maximum BPM",
                    "The highest tempo range the engine will initially search for.",
                    120.0f, 480.0f, 1.0f, 0, 240.0f
                ),
                SliderParam(
                    "hysteresisBlend", "Hysteresis Blend (Stickiness)",
                    "Determines how 'sticky' the current tempo is. Lower values resist drifting, higher values adapt to real tempo changes faster.",
                    0.01f, 0.2f, 0.01f, 2, 0.05f
                ),
                SliderParam(
                    "octaveMismatchTolerance", "Octave Lock Tolerance",
                    "Determines how long the engine will ignore sudden half-time or double-time drops before deciding the tempo change is intentional. Increase this for songs with long, sparse sections.",
                    10.0f, 300.0f, 10.0f, 0, 100.0f
                ),
                SliderParam(
                    "fastTempoThreshold", "Fast Tempo Threshold",
                    "BPM threshold above which fast-tempo heuristics and octave-correction logic become more aggressive to reduce half-time detection errors.",
                    100.0f, 200.0f, 1.0f, 0, 140.0f
                ),
                SliderParam(
                    "doubleTimeJumpThreshold", "Double-Time Jump Tolerance",
                    "The threshold required before the engine allows the BPM to suddenly double (e.g. jumping from 60 to 120).",
                    5.0f, 40.0f, 5.0f, 0, 20.0f
                ),
                SliderParam(
                    "halfTimeJumpThreshold", "Half-Time Jump Tolerance",
                    "The threshold required before the engine allows the BPM to suddenly halve (e.g. dropping from 120 to 60).",
                    5.0f, 30.0f, 5.0f, 0, 10.0f
                ),
                SliderParam(
                    "hardResetThreshold", "Hard Reset Threshold",
                    "If the tempo changes by more than this amount, the engine will instantly snap to the new BPM rather than smoothly gliding.",
                    5.0f, 50.0f, 5.0f, 0, 20.0f
                ),
                SliderParam(
                    "warmupHops", "Warmup Hops",
                    "Number of frames to wait on startup before producing a valid estimate.",
                    10.0f, 250.0f, 10.0f, 0, 50.0f
                ),
                SliderParam(
                    "lockEntryConfidence", "Lock Entry Confidence",
                    "Confidence must stay at or above this for 5 seconds before the BPM range locks. Higher = harder to lock in.",
                    0.50f, 0.95f, 0.05f, 2, 0.75f
                ),
                SliderParam(
                    "lockReleaseConfidence", "Lock Release Confidence",
                    "Confidence must drop to or below this for 5 seconds before the lock releases. Lower = harder to unlock.",
                    0.10f, 0.70f, 0.05f, 2, 0.50f
                ),
                SliderParam(
                    "lockedDriftTolerance", "Locked Drift Tolerance",
                    "Maximum BPM drift allowed while locked. Anything beyond this is rejected outright. Set to 0 for a completely frozen lock.",
                    0.0f, 10.0f, 0.5f, 1, 3.0f
                ),
            )
        ),
        ParamSection(
            "Display Output",
            listOf(
                SliderParam(
                    "uiSmoothing", "UI Smoothing (Alpha)",
                    "Controls the visual transition speed. Lower values look fluid on screen but will lag behind sudden, drastic tempo shifts.",
                    0.01f, 0.5f, 0.01f, 2, 0.15f
                ),
                SliderParam(
                    "visualDeadzone", "Visual Deadzone (Drift filter)",
                    "Filters out tiny algorithmic micro-fluctuations. Increase this to lock the display steady and stop it from jittering by fractions of a BPM.",
                    0.0f, 5.0f, 0.1f, 1, 0.1f
                ),
                SliderParam(
                    "magneticSnapRange", "Magnetic Snap Range",
                    "Automatically pulls decimal BPMs (like 127.85) to clean, readable values (128.0) for a better visual experience for live musicians.",
                    0.0f, 0.5f, 0.05f, 2, 0.15f
                ),
                SliderParam(
                    "confThreshold", "Confidence Threshold",
                    "The minimum confidence score required to update the BPM display. Higher values improve stability and reject uncertain detections.",
                    0.20f, 0.95f, 0.05f, 2, 0.50f
                ),
            )
        ),
    )

    /** Flat lookup of every slider by id. */
    val sliderParams: Map<String, SliderParam> =
        sections.flatMap { it.params }.associateBy { it.id }

    /**
     * Order of the numeric values returned by [NativeBridge.nativeLoadPreset]
     * (indices 0..23). MUST stay in lockstep with the out[] array in
     * jni_bridge.cpp's nativeLoadPreset.
     */
    val PRESET_PARAM_ORDER: List<String> = listOf(
        "onsetSensitivity",        // 0
        "complexDomainWeight",     // 1
        "fluxCompression",         // 2
        "noveltySmoothing",        // 3
        "silenceThreshold",        // 4
        "gatingAlpha",             // 5
        "silenceTimeoutSec",       // 6
        "dynamicWindowPercent",    // 7
        "minBpm",                  // 8
        "maxBpm",                  // 9
        "hysteresisBlend",         // 10
        "octaveMismatchTolerance", // 11
        "fastTempoThreshold",      // 12
        "doubleTimeJumpThreshold", // 13
        "halfTimeJumpThreshold",   // 14
        "hardResetThreshold",      // 15
        "warmupHops",              // 16
        "lockEntryConfidence",     // 17
        "lockReleaseConfidence",   // 18
        "lockedDriftTolerance",    // 19
        "uiSmoothing",             // 20
        "visualDeadzone",          // 21
        "magneticSnapRange",       // 22
        "confThreshold",           // 23
    )

    const val HEURISTICS_INDEX = 24

    /** Display-only params that also drive the Kotlin-side display smoothing. */
    const val P_UI_SMOOTHING = "uiSmoothing"
    const val P_VISUAL_DEADZONE = "visualDeadzone"
    const val P_MAGNETIC_SNAP = "magneticSnapRange"
    const val P_CONF_THRESHOLD = "confThreshold"

    val resolutions: List<ResolutionOption> = listOf(
        ResolutionOption(6, "Smart", "Auto-detects the most natural pulse and locks to master 1/4 tempo."),
        ResolutionOption(1, "1/2 Note", "Tracks the tempo on a half-note pulse."),
        ResolutionOption(2, "1/4 Note", "Tracks the tempo on a standard quarter-note pulse (Default)."),
        ResolutionOption(3, "1/8 Note", "Tracks the tempo on an 8th-note pulse."),
        ResolutionOption(4, "1/8 Note Triplet", "Tracks the tempo on an 8th-note triplet pulse (3 per beat)."),
        ResolutionOption(5, "1/16 Note", "Tracks the tempo on a 16th-note pulse."),
    )

    const val DEFAULT_RESOLUTION = 2

    val presets: List<PresetOption> = listOf(
        PresetOption("default", "Default / General Purpose"),
        PresetOption("cajon", "Cajón Flamenco (Percussive)"),
        PresetOption("flute", "Flute (Legato)"),
        PresetOption("guitar", "Guitar, Acoustic"),
        PresetOption("electricguitar", "Guitar, Electric"),
        PresetOption("acousticbass", "Bass, Acoustic"),
        PresetOption("electricbass", "Bass, Electric"),
        PresetOption("saxophone", "Saxophone (Alto / Tenor)"),
        PresetOption("liveband", "Full Band / Live Mix"),
        PresetOption("slowacoustic", "Ambient / Slow Acoustic"),
    )

    const val DEFAULT_PRESET = "default"

    /** Format a value with the given number of decimals (matches PARAM_DECIMALS in the web UI). */
    fun format(value: Float, decimals: Int): String =
        String.format(Locale.US, "%.${decimals}f", value)
}
