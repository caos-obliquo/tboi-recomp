#include "isaec/engine/clock.hpp"

#include <chrono>
#include <memory>

namespace isaec::engine {
namespace {

using TimePoint = std::chrono::steady_clock::time_point;

TimePoint to_timepoint(void* handle) {
    return handle == nullptr ? TimePoint{} : *static_cast<TimePoint*>(handle);
}

f64 seconds_between(TimePoint a, TimePoint b) {
    return std::chrono::duration<f64>(b - a).count();
}

}  // namespace

SteadyClock::SteadyClock() {
    const auto now = std::chrono::steady_clock::now();
    origin_ = new TimePoint(now);
    last_frame_ = new TimePoint(now);
}

SteadyClock::~SteadyClock() {
    delete static_cast<TimePoint*>(origin_);
    delete static_cast<TimePoint*>(last_frame_);
}

f64 SteadyClock::elapsed_seconds() const {
    return seconds_between(to_timepoint(origin_), std::chrono::steady_clock::now());
}

f64 SteadyClock::frame_seconds() {
    const auto now = std::chrono::steady_clock::now();
    const f64 delta = seconds_between(to_timepoint(last_frame_), now);
    *static_cast<TimePoint*>(last_frame_) = now;
    // A zero interval would make callers divide by zero; the smallest useful
    // frame time is a nanosecond.
    return delta > 0.0 ? delta : 1e-9;
}

}  // namespace isaec::engine
