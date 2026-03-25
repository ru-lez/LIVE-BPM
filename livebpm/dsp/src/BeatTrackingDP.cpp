#include "livebpm/dsp/BeatTrackingDP.h"
namespace livebpm { namespace dsp {
BeatTrackingResult BeatTrackingDP::track(const float* /*novelty*/, int
/*length*/,
 int /*beatRefLag*/, double
/*penaltyFactor*/) {
 return BeatTrackingResult{}; // TODO: implement in Week 3
}
}} // namespace livebpm::dsp