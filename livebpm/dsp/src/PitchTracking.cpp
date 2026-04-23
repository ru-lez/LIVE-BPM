#include "livebpm/dsp/PitchTracking.h"
#include <vector>
#include <cmath>

namespace livebpm { 
namespace dsp {

float PitchTracking::estimatePitchMPM(const float* audio, int numSamples, float sampleRate) {
    std::vector<float> nsdf(numSamples, 0.0f);
    
    // Calculate Normalized Square Difference Function (NSDF)
    for (int tau = 0; tau < numSamples; tau++) {
        float acf = 0.0f;
        float m = 0.0f;
        for (int j = 0; j < numSamples - tau; j++) {
            acf += audio[j] * audio[j + tau];
            m += audio[j] * audio[j] + audio[j + tau] * audio[j + tau];
        }
        if (m != 0.0f) {
            nsdf[tau] = 2.0f * acf / m;
        }
    }

    // Find first zero crossing to avoid lag=0 peak
    int zeroCrossing = 0;
    while (zeroCrossing < numSamples && nsdf[zeroCrossing] > 0.0f) {
        zeroCrossing++;
    }

    // Find highest peak after the zero crossing
    float maxPeakValue = -1.0f;
    int maxPeakIndex = -1;

    for (int i = zeroCrossing; i < numSamples - 1; i++) {
        if (nsdf[i] > nsdf[i - 1] && nsdf[i] > nsdf[i + 1]) {
            if (nsdf[i] > maxPeakValue) {
                maxPeakValue = nsdf[i];
                maxPeakIndex = i;
            }
        }
    }

    // Evaluate peak confidence
    // value of 0.8 is a standard threshold for MPM to make sure the pitch is clear
    if (maxPeakIndex > 0 && maxPeakValue > 0.8f) { 
        float y1 = nsdf[maxPeakIndex - 1];
        float y2 = nsdf[maxPeakIndex];
        float y3 = nsdf[maxPeakIndex + 1];
        
        // Parabolic interpolation for exact lag
        float d = (y1 - y3) / (2.0f * (y1 - 2.0f * y2 + y3));
        float interpolatedLag = maxPeakIndex + d;
        
        return sampleRate / interpolatedLag;
    }

    // Return 0.0 if the sound is unvoiced, percussive noise, or silent
    return 0.0f; 
}

}} // namespace livebpm::dsp