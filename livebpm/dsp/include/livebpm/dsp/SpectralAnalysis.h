#pragma once
#include <vector>
#include <cmath>
namespace livebpm { namespace dsp {
class SpectralAnalysis {
public:
 explicit SpectralAnalysis(int fftSize);
 ~SpectralAnalysis();
 // Applies Hann window + FFT. Writes to magnitude and phase.
 void compute(const float* samples,
 float* magnitude,
 float* phase);
 int getNumBins() const { return fftSize_ / 2 + 1; }
 int getFftSize() const { return fftSize_; }
private:
 int fftSize_;
 std::vector<float> hannWindow_;
 void* kissConfig_; // kiss_fft_cfg (opaque to avoid exposing kiss_fft.h)
 std::vector<float> fftInReal_;
 std::vector<float> fftInImag_;
 std::vector<float> fftOutReal_;
 std::vector<float> fftOutImag_;
};
}} // namespace livebpm::dsp