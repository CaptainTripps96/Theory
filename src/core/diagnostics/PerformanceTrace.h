#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace tsq::core::diagnostics
{
bool performanceTraceEnabled() noexcept;
std::int64_t performanceTraceThresholdMicros() noexcept;
void writePerformanceTrace (std::string_view label, std::int64_t elapsedMicros) noexcept;

class ScopedPerformanceTimer final
{
public:
    explicit ScopedPerformanceTimer (std::string label);
    ~ScopedPerformanceTimer();

    ScopedPerformanceTimer (const ScopedPerformanceTimer&) = delete;
    ScopedPerformanceTimer& operator= (const ScopedPerformanceTimer&) = delete;

private:
    using Clock = std::chrono::steady_clock;

    bool enabled_ = false;
    std::string label_;
    Clock::time_point start_ {};
};
}
