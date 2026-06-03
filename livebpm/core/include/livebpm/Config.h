#pragma once

#include <cstdint>

namespace livebpm {
    enum class NoteResolution {
        HalfNote = 1,           // Multiplies BPM by 2.0
        QuarterNote = 2,        // Default (x 1.0)
        EighthNote = 3,         // Multiplies BPM by 0.5
        EighthNoteTriplet = 4,  // Multiplies BPM by 0.6667 
        SixteenthNote = 5,       // Multiplies BPM by 0.25 
        SmartMixed = 6          // Dynamically choose between quarter and eighth based on signal characteristics
    };

    struct Config {
        double sampleRate = 48000.0;
        int blockSize = 512;
        int fftOrder = 11; // 2^11 = 2048
        double minBpm = 40.0;
        double maxBpm = 240.0;
        double analysisWindowSeconds = 4.0;

        // Hysteresis thresholds for octave error correction (Default: 20 BPM for double-time, 10 BPM for half-time)
        double doubleTimeJumpThreshold = 20.0;
        double halfTimeJumpThreshold = 10.0;

        // Noise Gating threshold (Default: 0.03)
        double SILENCE_THRESHOLD = 0.03;
        // Onset Sensitivity / Gating Offset (Default: 0.015)
        double onsetSensitivity = 0.015;
        // Complex Domain Weight [0.0 to 1.0] (Default: 0.4)
        double complexDomainWeight = 0.4;

        // UI Smoothing (Alpha)
        double uiSmoothing = 0.15;
        // Visual Deadzone for display smoothing (Default: 0.1 BPM For Accuracy, increase to ~0.2 BPM to prevent jitter)
        double VISUAL_DEADZONE = 0.1;
        // Magnetic Snapping
        double magneticSnapRange = 0.15;
        // Minimum confidence score required before accepting or displaying a tempo estimate
        double confThreshold = 0.50;

        // How fast the dynamic gate threshold adapts to volume changes
        double gatingAlpha = 0.1;
        // EMA alpha for novelty curve (higher = more responsive, less smooth)
        double noveltySmoothing = 0.4;
        // Log1p lambda for dynamic range compression
        double fluxCompression = 50.0;
        // Stable-BPM EMA alpha (lower = stickier/resists drift)
        double hysteresisBlend = 0.05;

        // Percentage range based on current stable BPM
        double dynamicWindowPercent = 0.40;
        // Break the lock and wide-open the bounds if confidence drops below this
        double shatterConfidenceThreshold = 0.70;
        // Max BPM drift allowed while locked; anything beyond this is rejected outright
        double lockedDriftTolerance = 3.0;

        double lockEntryConfidence  = 0.75;   // must be at/above this to build a lock
        double lockReleaseConfidence = 0.60;  // must be at/below this to release

        // Whether to apply heuristics for fast tempo detection and octave error correction
        bool useHeuristics = true; 

        // Duration of continuous silence before the detector resets or suspends
        double silenceTimeoutSec = 2.0;
        // Number of analysis hops required before tempo estimation is considered
        int warmupHops = 50;

        // BPM threshold above which fast-tempo heuristics and octave-correction
        double fastTempoThreshold = 140.0;
        // Hystersis tolerance before it breaks the lock
        int octaveMismatchTolerance = 100;
        // Tempo difference for instant snap
        double hardResetThreshold = 20.0;

        // Target note resolution for tempo estimation (Default: Quarter Notes)
        NoteResolution targetResolution = NoteResolution::QuarterNote;
    };

} // namespace livebpm