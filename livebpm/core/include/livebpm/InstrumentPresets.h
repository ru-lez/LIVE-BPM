#pragma once

#include "livebpm/Config.h"

// ---------------------------------------------------------------------------
// Instrument Presets
//
// Each preset tunes the runtime-configurable DSP parameters for the typical
// onset and timbral characteristics of a given instrument.  All structural
// fields (sampleRate, blockSize, fftOrder, analysisWindowSeconds) keep their
// engine defaults — only the parameters that are safe to change at runtime
// (see Engine::updateTunableConfig) are overridden here.
//
// Parameter summary (for quick reference while tuning):
//   semitoneFluxWeight  [0.0-1.0]  0=all complex-domain, 1=all semitone-flux
//   gatingOffset        [float]    onset sensitivity threshold above EMA
//   gatingAlpha         [0.0-1.0]  how fast the gate threshold adapts
//   noveltySmoothing    [0.0-1.0]  novelty EMA alpha (higher = more responsive)
//   fluxCompression     [float]    log1p lambda for dynamic range compression
//   hysteresisBlend     [0.0-1.0]  stable-BPM EMA alpha (lower = stickier)
//   SILENCE_THRESHOLD   [float]    RMS level below which the window is silent
//   minBpm / maxBpm     [BPM]      autocorrelation search range
//   halfTimeJumpThreshold / doubleTimeJumpThreshold   octave-lock protection
// ---------------------------------------------------------------------------

namespace livebpm {
namespace presets {

// ---------------------------------------------------------------------------
// Flute
//
// Onset character : Soft, breath-driven attacks with legato phrasing.
//                   Hard transients are rare — phase changes dominate.
// Frequency range : ~262 Hz – 4.2 kHz fundamentals, rich harmonics, vibrato.
// Key challenges  : Semitone flux is confused by vibrato modulation.
//                   Complex-domain flux catches the phase shift at each new note.
//                   Very soft in the lower register (needs a lower gate threshold).
//
// Tuning rationale:
//   semitoneFluxWeight 0.25  → rely heavily on complex-domain stream
//   gatingOffset       0.008 → catch very soft breath attacks
//   gatingAlpha        0.15  → adapt faster to breath-driven dynamics
//   noveltySmoothing   0.35  → slightly less smoothing so subtle attacks land
//   fluxCompression    60    → stronger compression for quiet signals
//   hysteresisBlend    0.04  → conservative — flute tempo is steady
//   SILENCE_THRESHOLD  0.015 → flute can be quieter than other instruments
// ---------------------------------------------------------------------------
inline Config flute() {
    Config cfg;
    cfg.semitoneFluxWeight     = 0.25f;
    cfg.gatingOffset           = 0.008f;
    cfg.gatingAlpha            = 0.15f;
    cfg.noveltySmoothing       = 0.35f;
    cfg.fluxCompression        = 60.0f;
    cfg.hysteresisBlend        = 0.04f;
    cfg.SILENCE_THRESHOLD      = 0.015;
    cfg.minBpm                 = 50.0;
    cfg.maxBpm                 = 200.0;
    return cfg;
}

// ---------------------------------------------------------------------------
// Guitar (acoustic)
//
// Onset character : Clear picking or strumming attacks — broadband, sharp.
//                   String resonance creates gradual inter-note energy.
// Frequency range : ~82 Hz – 1.2 kHz fundamentals, harmonics up to ~5 kHz.
// Key challenges  : Alternating strum patterns can trigger the double-time veto.
//                   Chord sustain produces low-level flux between real onsets.
//
// Tuning rationale:
//   semitoneFluxWeight 0.65  → strong semitone content from steel/nylon strings
//   gatingOffset       0.018 → slightly above default to ignore string resonance
//   noveltySmoothing   0.45  → a bit more smoothing to ride through ring-out
//   hysteresisBlend    0.07  → allow slightly faster tracking for dynamic strumming
// ---------------------------------------------------------------------------
inline Config guitar() {
    Config cfg;
    cfg.semitoneFluxWeight     = 0.65f;
    cfg.gatingOffset           = 0.018f;
    cfg.gatingAlpha            = 0.10f;
    cfg.noveltySmoothing       = 0.45f;
    cfg.fluxCompression        = 50.0f;
    cfg.hysteresisBlend        = 0.07f;
    cfg.SILENCE_THRESHOLD      = 0.03;
    cfg.minBpm                 = 40.0;
    cfg.maxBpm                 = 240.0;
    return cfg;
}

// ---------------------------------------------------------------------------
// Saxophone (alto / tenor)
//
// Onset character : Reed transient (moderate sharpness) + breath attack.
//                   Ranges from staccato to very legato depending on style.
// Frequency range : ~140 Hz – 3.5 kHz fundamentals, dense harmonics.
// Key challenges  : Slurs and glissandi look like a continuous onset — both
//                   streams contribute.  Vibrato creates amplitude modulation
//                   that can produce spurious semitone-flux spikes.
//
// Tuning rationale:
//   semitoneFluxWeight 0.45  → balanced — reed gives transients, breath gives phase
//   gatingOffset       0.012 → lower to catch softer legato passages
//   gatingAlpha        0.12  → slightly faster adaptation for jazz dynamics
//   fluxCompression    55    → slightly stronger to handle wide dynamic range
//   SILENCE_THRESHOLD  0.020 → breath noise sits just above a standard gate
// ---------------------------------------------------------------------------
inline Config saxophone() {
    Config cfg;
    cfg.semitoneFluxWeight     = 0.45f;
    cfg.gatingOffset           = 0.012f;
    cfg.gatingAlpha            = 0.12f;
    cfg.noveltySmoothing       = 0.40f;
    cfg.fluxCompression        = 55.0f;
    cfg.hysteresisBlend        = 0.05f;
    cfg.SILENCE_THRESHOLD      = 0.020;
    cfg.minBpm                 = 40.0;
    cfg.maxBpm                 = 240.0;
    return cfg;
}

// ---------------------------------------------------------------------------
// Acoustic Bass (double bass / bass guitar)
//
// Onset character : Moderate pluck/bow attack with long, overlapping sustain.
//                   Bowed notes have very soft onsets — complex-domain matters.
// Frequency range : ~41 Hz – 300 Hz fundamentals (below the filter-bank floor
//                   at 55 Hz for open E — some energy may be missed at lowest notes).
// Key challenges  : Long sustain blurs note boundaries; the engine can confuse
//                   a walking-bass line for a half-time pattern.
//                   Low-level rumble can sit above a default silence gate.
//
// Tuning rationale:
//   semitoneFluxWeight 0.50  → balanced — bowed and plucked techniques both used
//   gatingAlpha        0.08  → slower adaptation — bass dynamics are very steady
//   noveltySmoothing   0.30  → less smoothing so each distinct pluck/bow is detected
//   fluxCompression    45    → less compression — preserve attack dynamics
//   hysteresisBlend    0.04  → sticky — bass marks very steady beats
//   SILENCE_THRESHOLD  0.025 → slightly higher to reject low-level room rumble
//   halfTimeJumpThreshold 15 → more tolerance for half-time bass patterns
//   minBpm             30    → bass accompanies slow music (ballads, blues)
// ---------------------------------------------------------------------------
inline Config acousticBass() {
    Config cfg;
    cfg.semitoneFluxWeight     = 0.50f;
    cfg.gatingOffset           = 0.020f;
    cfg.gatingAlpha            = 0.08f;
    cfg.noveltySmoothing       = 0.30f;
    cfg.fluxCompression        = 45.0f;
    cfg.hysteresisBlend        = 0.04f;
    cfg.SILENCE_THRESHOLD      = 0.025;
    cfg.halfTimeJumpThreshold  = 15.0;
    cfg.minBpm                 = 30.0;
    cfg.maxBpm                 = 200.0;
    return cfg;
}

// ---------------------------------------------------------------------------
// Cajón Flamenco
//
// Onset character : Extremely sharp percussive hits.
//                   Tapa (slap) = very short broadband transient.
//                   Bass (golpe) = strong low thump.
// Frequency range : Bass thump ~80–300 Hz + crisp slap at 2–8 kHz.
// Key challenges  : Flamenco compás (especially Bulería) uses irregular 12-beat
//                   cycles with strong accents on beats 3, 6, 8, 10, 11, 12.
//                   The asymmetric accent pattern can trigger the alternation veto.
//                   Resonance from the wooden box rings out between hits.
//                   Bulerías can exceed 200 BPM in the individual-beat pulse.
//
// Tuning rationale:
//   semitoneFluxWeight 0.80  → percussive instrument — semitone flux dominates
//   gatingOffset       0.025 → higher threshold to avoid box resonance bleed
//   gatingAlpha        0.08  → slower gate adaptation — don't let one loud hit
//                              reset the threshold and cause missed follow-up hits
//   noveltySmoothing   0.50  → more smoothing to tame resonant body decay
//   fluxCompression    45    → less compression — preserve the full hit dynamics
//   hysteresisBlend    0.06  → slightly faster to track flamenco accelerandos
//   doubleTimeJumpThreshold 15  → stricter octave guard for complex rhythmic cells
//   maxBpm             280   → Bulería at full speed can push past 240 BPM
// ---------------------------------------------------------------------------
inline Config cajonFlamenco() {
    Config cfg;
    cfg.semitoneFluxWeight        = 0.80f;
    cfg.gatingOffset              = 0.025f;
    cfg.gatingAlpha               = 0.08f;
    cfg.noveltySmoothing          = 0.50f;
    cfg.fluxCompression           = 45.0f;
    cfg.hysteresisBlend           = 0.06f;
    cfg.SILENCE_THRESHOLD         = 0.03;
    cfg.doubleTimeJumpThreshold   = 15.0;
    cfg.minBpm                    = 40.0;
    cfg.maxBpm                    = 280.0;
    return cfg;
}

} // namespace presets
} // namespace livebpm
