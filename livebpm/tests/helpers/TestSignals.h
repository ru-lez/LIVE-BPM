#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#pragma once
#include <vector>
#include <cmath>
namespace livebpm { namespace test {
// Click track: short pulses at a given BPM
inline std::vector<float> generateClickTrack(
 double bpm, double sampleRate, double durationSeconds,
 double clickDurationMs = 5.0, double clickFreqHz = 1000.0)
{
 int totalSamples = static_cast<int>(sampleRate * durationSeconds);
 std::vector<float> signal(totalSamples, 0.0f);
 double beatIntervalSamples = 60.0 / bpm * sampleRate;
 int clickSamples = static_cast<int>(clickDurationMs * sampleRate /
1000.0);
 for (double pos = 0; pos < totalSamples; pos +=
beatIntervalSamples) {
 int start = static_cast<int>(pos);
 for (int i = 0; i < clickSamples && (start + i) <
totalSamples; ++i) {
 signal[start + i] = 0.8f * std::sin(
 2.0f * static_cast<float>(M_PI) *
static_cast<float>(clickFreqHz)
 * i / static_cast<float>(sampleRate));
 }
 }
 return signal;
 }
// Synthetic novelty function (for DP tests without needing FFT)
inline std::vector<float> generatePeriodicNovelty(
 double bpm, double featureRate, double durationSeconds,
 double noiseLevel = 0.0)
{
 int length = static_cast<int>(featureRate * durationSeconds);
 std::vector<float> novelty(length, 0.0f);
 double beatIntervalFrames = 60.0 / bpm * featureRate;
 for (double pos = 0; pos < length; pos += beatIntervalFrames) {
 int idx = static_cast<int>(pos);
 if (idx < length) novelty[idx] = 1.0f;
 }
 if (noiseLevel > 0.0) {
 // Simple noise (not cryptographic, just for tests)
 unsigned seed = 42;
 for (int i = 0; i < length; ++i) {
 seed = seed * 1103515245 + 12345;
 float noise = static_cast<float>(seed % 1000) / 1000.0f;
 novelty[i] += static_cast<float>(noiseLevel) * noise;
 }
 }
 return novelty;
}
}} // namespace livebpm::test