// Monotonic clock.
//
// Backed by std::chrono in the core; a platform layer can supply its own if it
// needs to tie frame pacing to a native vsync signal. The interface exists so
// the engine never calls a timing function directly.

#ifndef ISAEC_ENGINE_CLOCK_HPP
#define ISAEC_ENGINE_CLOCK_HPP

#include "isaec/platform/types.hpp"

namespace isaec::engine {

class Clock {
public:
    virtual ~Clock() = default;

    // Seconds since the clock was created.
    [[nodiscard]] virtual f64 elapsed_seconds() const = 0;

    // Seconds since the previous frame_marker() call, then resets the frame
    // reference. Returns a small positive floor when the interval rounds to
    // zero, so callers never divide by exactly zero.
    virtual f64 frame_seconds() = 0;

protected:
    Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
};

// Default implementation over std::chrono::steady_clock.
class SteadyClock final : public Clock {
public:
    SteadyClock();
    ~SteadyClock() override;

    SteadyClock(const SteadyClock&) = delete;
    SteadyClock& operator=(const SteadyClock&) = delete;

    [[nodiscard]] f64 elapsed_seconds() const override;
    f64 frame_seconds() override;

private:
    void* origin_ = nullptr;  // steady_clock::time_point, heap-held to stay header-clean
    void* last_frame_ = nullptr;
};

}  // namespace isaec::engine

#endif  // ISAEC_ENGINE_CLOCK_HPP
