// LiveBPM offline accuracy harness.
//
// Runs the full detection engine over audio with a KNOWN tempo and reports how
// close the estimate lands — no microphone or device needed, so it can run in
// CI or from the command line.
//
// Usage:
//   livebpm_accuracy                     synthetic suite (clicks at known BPMs)
//   livebpm_accuracy --preset liveband   synthetic suite with an instrument preset
//   livebpm_accuracy manifest.csv        real audio files listed in a manifest
//
// Manifest format (one file per line, paths relative to the manifest):
//   # path, expected_bpm [, preset]
//   billiejean.wav, 117
//   funky_drummer.wav, 101, liveband
//
// Each case reports, besides the median/confidence verdict:
//   lock   seconds from the start of the clip until the estimate first held
//          ON TARGET continuously for LOCK_HOLD_SEC ("never" if it didn't)
//   stable % of estimates from that lock point to the end that stayed ON TARGET
//   flips  how many times, after locking, the reading left target and returned
// These catch what the median alone hides: a slow lock or a shaky one.
//
// Exit code: 0 if every case lands ON TARGET (±tolerance), 1 otherwise.

// Ignoring third-party libraries for -Wall -Wextra -Wpedantic
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

// Stop windows.h (pulled in by miniaudio) from defining min/max macros that
// break std::min/std::max on MSVC.
#define NOMINMAX

#define MA_NO_DEVICE_IO
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "livebpm/Engine.h"
#include "livebpm/Presets.h"
#include "livebpm/dsp/Biquad.h"
#include "TestSignals.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr double SAMPLE_RATE = 48000.0;
constexpr int    BLOCK_SIZE  = 1024;   // matches the Android capture path
constexpr double WARMUP_SEC  = 6.0;    // ignore estimates before this for the median/confidence aggregate
constexpr double TOLERANCE   = 3.0;    // "on target" band in BPM
constexpr double LOCK_HOLD_SEC = 2.0;  // sustained on-target duration required to call it "locked"
constexpr double GAP_BREAK_SEC = 1.0;  // a real-time gap between valid estimates breaks a streak

enum class Verdict { OnTarget, DoubleTime, HalfTime, Off, None };

const char* verdictName(Verdict v) {
    switch (v) {
        case Verdict::OnTarget:   return "ON TARGET";
        case Verdict::DoubleTime: return "DOUBLE (2x)";
        case Verdict::HalfTime:   return "HALF (0.5x)";
        case Verdict::Off:        return "OFF";
        default:                  return "NO ESTIMATE";
    }
}

Verdict classify(double bpm, double expected) {
    if (bpm <= 0.0) return Verdict::None;
    if (std::abs(bpm - expected) <= TOLERANCE) return Verdict::OnTarget;
    if (std::abs(bpm - 2.0 * expected) <= 2.0 * TOLERANCE) return Verdict::DoubleTime;
    if (std::abs(bpm - expected / 2.0) <= TOLERANCE) return Verdict::HalfTime;
    return Verdict::Off;
}

// One valid estimate, timestamped from the start of the clip (not from the
// aggregate warm-up cutoff) so lock-time reflects what a musician actually
// experiences: elapsed time from pressing "Listen" to a trustworthy readout.
struct Sample {
    double timeSec;
    double bpm;
    double confidence;
};

struct CaseResult {
    std::string name;
    double expected = 0.0;
    double median = 0.0;
    double meanConfidence = 0.0;
    int samples = 0;

    // Time-to-lock / stability metrics (see computeLockMetrics). -1 = never
    // locked within the clip (insufficient sustained on-target data).
    double lockTimeSec = -1.0;
    double stabilityPct = -1.0;
    int flipCount = 0;

    Verdict verdict = Verdict::None;
};

// Find the first sustained on-target streak (>= LOCK_HOLD_SEC, no gap wider
// than GAP_BREAK_SEC) and, from there to the end of the clip, measure what
// fraction of estimates stayed on target and how many times the reading left
// target and came back. This is the metric that actually matters live: the
// median alone can look perfect while hiding a shaky lock or frequent flips.
void computeLockMetrics(const std::vector<Sample>& samples, double expected, CaseResult& out) {
    if (samples.empty()) return;

    std::vector<Verdict> verdicts(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        verdicts[i] = classify(samples[i].bpm, expected);
    }

    size_t runStart = 0;
    bool locked = false;
    for (size_t i = 0; i < samples.size(); ++i) {
        bool breakStreak = (verdicts[i] != Verdict::OnTarget);
        if (!breakStreak && i > runStart) {
            double gap = samples[i].timeSec - samples[i - 1].timeSec;
            if (gap > GAP_BREAK_SEC) breakStreak = true;
        }
        if (breakStreak) {
            runStart = i + 1;
            continue;
        }
        if (samples[i].timeSec - samples[runStart].timeSec >= LOCK_HOLD_SEC) {
            out.lockTimeSec = samples[runStart].timeSec;
            locked = true;
            break;
        }
    }
    if (!locked) return;

    int onTargetCount = 0;
    int total = 0;
    int flips = 0;
    bool prevOnTarget = true; // verdicts[runStart] == OnTarget by construction
    for (size_t i = runStart; i < samples.size(); ++i) {
        total++;
        bool isOn = (verdicts[i] == Verdict::OnTarget);
        if (isOn) onTargetCount++;
        if (i > runStart && isOn != prevOnTarget) flips++;
        prevOnTarget = isOn;
    }
    out.stabilityPct = 100.0 * static_cast<double>(onTargetCount) / static_cast<double>(total);
    out.flipCount = flips;
}

// Stream a signal through a fresh engine exactly like the live paths do
// (block-wise, 80 Hz high-passed) and collect every valid estimate.
CaseResult runCase(const std::string& name, const std::vector<float>& signal,
                   double expectedBpm, const std::string& preset) {
    livebpm::Config config = livebpm::GetPreset(preset);
    config.sampleRate = SAMPLE_RATE;
    config.blockSize  = BLOCK_SIZE;

    livebpm::Engine engine(config);
    engine.updateTunableConfig(config);

    livebpm::dsp::Biquad highpass;
    highpass.setCoefficients(livebpm::dsp::Biquad::makeHighpass(SAMPLE_RATE, 80.0));

    std::vector<float> block(BLOCK_SIZE);
    std::vector<Sample> samples;

    for (size_t offset = 0; offset + BLOCK_SIZE <= signal.size(); offset += BLOCK_SIZE) {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            block[static_cast<size_t>(i)] = highpass.process(signal[offset + static_cast<size_t>(i)]);
        }
        engine.processBlock(block.data(), BLOCK_SIZE);

        auto r = engine.getLatestResult();
        if (r.hasValidEstimate && r.bpm > 0.0) {
            samples.push_back(Sample{static_cast<double>(offset) / SAMPLE_RATE, r.bpm, r.confidence});
        }
    }

    CaseResult out;
    out.name = name;
    out.expected = expectedBpm;

    // Median/confidence aggregate: only over the post-warm-up tail, so early
    // spin-up noise doesn't skew the headline BPM/confidence figures.
    std::vector<double> bpms;
    double confidenceSum = 0.0;
    for (const auto& s : samples) {
        if (s.timeSec < WARMUP_SEC) continue;
        bpms.push_back(s.bpm);
        confidenceSum += s.confidence;
    }
    out.samples = static_cast<int>(bpms.size());
    if (!bpms.empty()) {
        std::sort(bpms.begin(), bpms.end());
        out.median = bpms[bpms.size() / 2];
        out.meanConfidence = confidenceSum / static_cast<double>(bpms.size());
        out.verdict = classify(out.median, expectedBpm);
    }

    // Lock/stability metrics run over the FULL timeline (from t=0), since
    // "how long until it locks" is only meaningful measured from the start.
    computeLockMetrics(samples, expectedBpm, out);
    return out;
}

// Click track with an accented backbeat (loud/soft alternation) — stresses the
// alternation/octave heuristics harder than a uniform click.
std::vector<float> generateAccentedClicks(double bpm, double durationSeconds) {
    auto signal = livebpm::test::generateClickTrack(bpm, SAMPLE_RATE, durationSeconds);
    double beatIntervalSamples = (60.0 / bpm) * SAMPLE_RATE;
    int beat = 0;
    for (double pos = 0; pos < static_cast<double>(signal.size()); pos += beatIntervalSamples, ++beat) {
        if (beat % 2 == 1) {
            size_t start = static_cast<size_t>(pos);
            size_t end = std::min(signal.size(), start + static_cast<size_t>(SAMPLE_RATE * 0.01));
            for (size_t i = start; i < end; ++i) signal[i] *= 0.45f;
        }
    }
    return signal;
}

std::vector<float> withNoise(std::vector<float> signal, float level) {
    unsigned seed = 1234;
    for (auto& s : signal) {
        seed = seed * 1103515245u + 12345u;
        float n = (static_cast<float>(seed % 2000) / 1000.0f) - 1.0f; // [-1, 1)
        s += level * n;
    }
    return signal;
}

bool decodeWavMono48k(const std::string& path, std::vector<float>& out) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1,
                                                   static_cast<ma_uint32>(SAMPLE_RATE));
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS) return false;

    std::vector<float> chunk(48000);
    ma_uint64 framesRead = 0;
    for (;;) {
        if (ma_decoder_read_pcm_frames(&decoder, chunk.data(), chunk.size(), &framesRead) != MA_SUCCESS
            || framesRead == 0) break;
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<size_t>(framesRead));
    }
    ma_decoder_uninit(&decoder);
    return !out.empty();
}

std::string dirOf(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? std::string() : path.substr(0, p + 1);
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
}

void printResult(const CaseResult& r) {
    std::printf("  %-38s expect %6.1f  got %6.1f  conf %3.0f%%  n=%-4d %s",
                r.name.c_str(), r.expected, r.median, r.meanConfidence * 100.0,
                r.samples, verdictName(r.verdict));
    if (r.lockTimeSec >= 0.0) {
        std::printf("  lock %5.1fs  stable %3.0f%%  flips %d\n",
                    r.lockTimeSec, r.stabilityPct, r.flipCount);
    } else {
        std::printf("  lock   never\n");
    }
}

} // namespace

int main(int argc, char* argv[]) {
    std::string preset = "default";
    std::string manifestPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--preset" && i + 1 < argc) preset = argv[++i];
        else if (arg == "--synthetic") { /* default mode */ }
        else manifestPath = arg;
    }

    std::vector<CaseResult> results;

    if (manifestPath.empty()) {
        std::cout << "== LiveBPM accuracy: synthetic suite (preset: " << preset << ") ==\n";
        const double tempos[] = {60.0, 90.0, 117.0, 120.0, 140.0, 180.0};
        const double DURATION = 14.0;

        for (double bpm : tempos) {
            char name[64];

            std::snprintf(name, sizeof(name), "click %.0f BPM", bpm);
            results.push_back(runCase(
                name, livebpm::test::generateClickTrack(bpm, SAMPLE_RATE, DURATION), bpm, preset));

            std::snprintf(name, sizeof(name), "accented (loud/soft) %.0f BPM", bpm);
            results.push_back(runCase(
                name, generateAccentedClicks(bpm, DURATION), bpm, preset));

            std::snprintf(name, sizeof(name), "click + noise %.0f BPM", bpm);
            results.push_back(runCase(
                name, withNoise(livebpm::test::generateClickTrack(bpm, SAMPLE_RATE, DURATION), 0.02f),
                bpm, preset));
        }
    } else {
        std::cout << "== LiveBPM accuracy: manifest " << manifestPath << " ==\n";
        std::ifstream manifest(manifestPath);
        if (!manifest) {
            std::cerr << "Cannot open manifest: " << manifestPath << "\n";
            return 2;
        }
        std::string base = dirOf(manifestPath);
        std::string line;
        while (std::getline(manifest, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            std::stringstream ss(line);
            std::string file, bpmStr, rowPreset;
            std::getline(ss, file, ',');
            std::getline(ss, bpmStr, ',');
            std::getline(ss, rowPreset, ',');
            file = trim(file);
            rowPreset = trim(rowPreset);
            if (rowPreset.empty()) rowPreset = preset;

            double expected = 0.0;
            try { expected = std::stod(trim(bpmStr)); } catch (...) {}
            if (file.empty() || expected <= 0.0) {
                std::cerr << "  skipping malformed line: " << line << "\n";
                continue;
            }

            std::string fullPath = file;
            // Resolve relative to the manifest's directory.
            if (!base.empty() && file.find(':') == std::string::npos && file[0] != '/' && file[0] != '\\') {
                fullPath = base + file;
            }

            std::vector<float> audio;
            if (!decodeWavMono48k(fullPath, audio)) {
                std::cerr << "  FAILED to decode: " << fullPath << "\n";
                CaseResult r; r.name = file; r.expected = expected;
                results.push_back(r);
                continue;
            }
            results.push_back(runCase(file, audio, expected, rowPreset));
        }
    }

    int onTarget = 0, octave = 0, off = 0, none = 0, neverLocked = 0;
    double lockTimeSum = 0.0, stabilitySum = 0.0;
    int lockedCount = 0;
    std::cout << "\n";
    for (const auto& r : results) {
        printResult(r);
        switch (r.verdict) {
            case Verdict::OnTarget: onTarget++; break;
            case Verdict::DoubleTime:
            case Verdict::HalfTime: octave++; break;
            case Verdict::Off: off++; break;
            default: none++; break;
        }
        if (r.lockTimeSec >= 0.0) {
            lockTimeSum += r.lockTimeSec;
            stabilitySum += r.stabilityPct;
            lockedCount++;
        } else if (r.samples > 0) {
            neverLocked++;
        }
    }

    std::cout << "\nSummary: " << onTarget << "/" << results.size() << " on target (±"
              << TOLERANCE << " BPM), " << octave << " octave, " << off << " off, "
              << none << " no estimate\n";

    if (lockedCount > 0) {
        std::printf("Lock/stability (over %d cases that locked): avg lock time %.1fs, avg stability %.0f%%\n",
                    lockedCount, lockTimeSum / lockedCount, stabilitySum / lockedCount);
    }
    if (neverLocked > 0) {
        std::cout << neverLocked << " case(s) produced estimates but never sustained a "
                  << LOCK_HOLD_SEC << "s on-target lock.\n";
    }

    return (onTarget == static_cast<int>(results.size())) ? 0 : 1;
}
