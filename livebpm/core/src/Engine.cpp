#include "livebpm/Engine.h"
namespace livebpm {
Engine::Engine(const Config& config) : config_(config) {}
Engine::~Engine() = default;
void Engine::processBlock(const float* /*samples*/, int
/*numSamples*/) {
 // TODO: implement in Week 2-3
}
Result Engine::getLatestResult() const {
 return latestResult_;
}
void Engine::reset() {
 latestResult_ = Result{};
}
} // namespace livebpm