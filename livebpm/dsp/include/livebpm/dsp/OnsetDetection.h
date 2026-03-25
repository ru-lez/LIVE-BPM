#pragma once
namespace livebpm { namespace dsp {
class OnsetDetection {
public:
 // Spectral Flux (half-wave rectified)
 static float spectralFlux(const float* currentMag,
 const float* previousMag,
 int numBins);
};
}} // namespace livebpm::dsp