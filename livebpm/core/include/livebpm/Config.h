#pragma once

#include <cstdint>

namespace livebpm {
    enum class NoteResolution {
        HalfNote = 1,     // Multiplies BPM by 2.0
        QuarterNote = 2,  // Default (x 1.0)
        EighthNote = 3    // Multiplies BPM by 0.5
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

        // Noise Gating threshold (Default: 0.02)
        double SILENCE_THRESHOLD = 0.02;

        NoteResolution targetResolution = NoteResolution::QuarterNote; // Default to quarter notes
    };

} // namespace livebpm