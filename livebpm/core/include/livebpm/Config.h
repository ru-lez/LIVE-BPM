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

        // Visual Deadzone for display smoothing (Default: 0.1 BPM For Accuracy, increase to ~0.2 BPM to prevent jitter)
        double VISUAL_DEADZONE = 0.1;

        // Whether to apply heuristics for fast tempo detection and octave error correction
        bool useHeuristics = true; 

        // Target note resolution for tempo estimation (Default: Quarter Notes)
        NoteResolution targetResolution = NoteResolution::QuarterNote;
    };

} // namespace livebpm