#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "livebpm/dsp/SpectralAnalysis.h"
#include "kiss_fft.h"
namespace livebpm { namespace dsp {
SpectralAnalysis::SpectralAnalysis(int fftSize)
 : fftSize_(fftSize), hannWindow_(fftSize) {
 // Pre-compute Hann window
 for (int i = 0; i < fftSize; ++i) {
 hannWindow_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i /
(fftSize - 1)));
 }
 kissConfig_ = kiss_fft_alloc(fftSize, 0, nullptr, nullptr);
 // TODO: allocate internal buffers
}
SpectralAnalysis::~SpectralAnalysis() {
 kiss_fft_free(kissConfig_);
}
void SpectralAnalysis::compute(const float* /*samples*/,
 float* /*magnitude*/,
 float* /*phase*/) {
 // TODO: implement in Week 2
}
}} // namespace livebpm::dsp