#pragma once
#include <vector>
namespace livebpm { namespace dsp {
struct BeatTrackingResult {
 std::vector<int> beatIndices; // beat positions in the novelty
 double stability = 0.0; // [0, 1]
 double alignment = 0.0; // [0, 1]
 double coverage = 0.0; // [0, 1]
};
class BeatTrackingDP {
public:
 BeatTrackingDP() = default;
 BeatTrackingResult track(const float* novelty, int length,
 int beatRefLag, double penaltyFactor =
100.0);
private:
 std::vector<double> cost_;
 std::vector<int> backlink_;
 std::vector<double> penalty_;
};
}} // namespace livebpm::dsp