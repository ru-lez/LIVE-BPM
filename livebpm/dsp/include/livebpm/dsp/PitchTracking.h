#pragma once

namespace livebpm {
namespace dsp {

class PitchTracking {
public:
    static float estimatePitchMPM(const float* audio,
                                  int numSamples, 
                                  float sampleRate);
};

}} // namespace livebpm::dsp