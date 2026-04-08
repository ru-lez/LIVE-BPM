#include "livebpm/dsp/BeatTrackingDP.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>

namespace livebpm {
namespace dsp {

BeatTrackingResult BeatTrackingDP::track(
    const float* novelty,
    int length,
    int beatRefLag,
    double penaltyFactor)
{
    if (length < 2 || beatRefLag < 1)
        return BeatTrackingResult{};

    // --- Step 1: Pre-compute penalty function ---
    // Search range: [beatRef/2, beatRef*2]
    int minLag = std::max(1, beatRefLag / 2);
    int maxLag = std::min(length - 1, beatRefLag * 2);

    penalty_.resize(maxLag + 1, 0.0);

    for (int d = minLag; d <= maxLag; ++d) {
        double ratio = static_cast<double>(d) / beatRefLag;
        double logRatio = std::log2(ratio);
        penalty_[d] = -penaltyFactor * logRatio * logRatio;
    }

    // --- Step 2: Forward pass ---
    cost_.assign(length, 0.0);
    backlink_.assign(length, -1);

    for (int i = 0; i < length; ++i) {
        cost_[i] = static_cast<double>(novelty[i]);

        // Find the best predecessor
        double bestPrev = -1e30;
        int bestJ = -1;

        int searchStart = std::max(0, i - maxLag);
        int searchEnd   = std::max(0, i - minLag);

        for (int j = searchStart; j <= searchEnd; ++j) {
            int delta = i - j;

            if (delta < minLag || delta > maxLag)
                continue;

            double score = cost_[j] + penalty_[delta];

            if (score > bestPrev) {
                bestPrev = score;
                bestJ = j;
            }
        }

        if (bestJ >= 0) {
            cost_[i] += bestPrev;
            backlink_[i] = bestJ;
        }
    }

    // --- Step 3: Backtracking ---
    int endIdx = 0;
    double maxCost = cost_[0];

    for (int i = 1; i < length; ++i) {
        if (cost_[i] > maxCost) {
            maxCost = cost_[i];
            endIdx = i;
        }
    }

    std::vector<int> beats;
    int current = endIdx;

    while (current >= 0) {
        beats.push_back(current);
        current = backlink_[current];
    }

    std::reverse(beats.begin(), beats.end());

    // --- Step 4: Compute metrics ---
    BeatTrackingResult result;
    result.beatIndices = beats;

    if (beats.size() >= 2) {

        // Stability: 1 - coefficient of variation of intervals
        std::vector<double> intervals;

        for (size_t i = 1; i < beats.size(); ++i) {
            intervals.push_back(
                static_cast<double>(beats[i] - beats[i - 1])
            );
        }

        double mean =
            std::accumulate(intervals.begin(), intervals.end(), 0.0)
            / intervals.size();

        double variance = 0.0;

        for (double iv : intervals) {
            double diff = iv - mean;
            variance += diff * diff;
        }

        variance /= intervals.size();

        double cv = (mean > 0)
                        ? std::sqrt(variance) / mean
                        : 1.0;

        result.stability = std::max(0.0, 1.0 - cv);

        // Alignment: mean novelty at beat positions / max novelty
        float maxNovelty =
            *std::max_element(novelty, novelty + length);

        if (maxNovelty > 0) {
            double sumNovelty = 0.0;

            for (int idx : beats) {
                sumNovelty += novelty[idx];
            }

            result.alignment =
                sumNovelty / (beats.size() * maxNovelty);
        }

        // Coverage: fraction of the time window covered by beats
        int span = beats.back() - beats.front();

        result.coverage =
            (length > 0)
                ? static_cast<double>(span) / length
                : 0.0;
    }

    return result;
}

} // namespace dsp
} // namespace livebpm