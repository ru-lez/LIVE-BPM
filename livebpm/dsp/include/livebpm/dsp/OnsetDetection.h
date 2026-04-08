#pragma once
namespace livebpm { namespace dsp {
class OnsetDetection {
public:
 // Spectral Flux (half-wave rectified)
 static float spectralFlux(const float* currentMag,
 const float* previousMag,
 int numBins);


 static float complexDomain(
    const float* currentMag,
    const float* currentPhase,
    const float* prevMag,
    const float* prevPhase,
    const float* prevPrevPhase,
    int numBins
);

};



}} // namespace livebpm::dsp