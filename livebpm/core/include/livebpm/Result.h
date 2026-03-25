#pragma once
namespace livebpm {
struct Result {
 double bpm = 0.0;
 double confidence = 0.0;
 bool hasValidEstimate = false;
 double processingTimeMs = 0.0;
};
} // namespace livebpm