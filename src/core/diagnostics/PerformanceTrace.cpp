#include "core/diagnostics/PerformanceTrace.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <algorithm>
#include <utility>

namespace tsq::core::diagnostics
{
namespace
{
bool envFlagEnabled (const char* value) noexcept
{
    if (value == nullptr || *value == '\0')
        return false;

    return std::strcmp (value, "0") != 0
        && std::strcmp (value, "false") != 0
        && std::strcmp (value, "FALSE") != 0
        && std::strcmp (value, "off") != 0
        && std::strcmp (value, "OFF") != 0;
}

std::int64_t parseThresholdMicros() noexcept
{
    if (const auto* micros = std::getenv ("TSQ_PERF_THRESHOLD_US"))
        return std::max<std::int64_t> (0, std::atoll (micros));

    if (const auto* millis = std::getenv ("TSQ_PERF_THRESHOLD_MS"))
        return std::max<std::int64_t> (0, std::atoll (millis) * 1000);

    return 1000;
}

std::mutex& outputMutex()
{
    static std::mutex mutex;
    return mutex;
}
}

bool performanceTraceEnabled() noexcept
{
    static const auto enabled = envFlagEnabled (std::getenv ("TSQ_PERF_TRACE"));
    return enabled;
}

std::int64_t performanceTraceThresholdMicros() noexcept
{
    static const auto thresholdMicros = parseThresholdMicros();
    return thresholdMicros;
}

void writePerformanceTrace (std::string_view label, std::int64_t elapsedMicros) noexcept
{
    if (! performanceTraceEnabled() || elapsedMicros < performanceTraceThresholdMicros())
        return;

    try
    {
        const std::lock_guard lock { outputMutex() };
        std::clog << "[perf] " << elapsedMicros << " us "
                  << label << " thread=" << std::this_thread::get_id() << '\n';
    }
    catch (...)
    {
    }
}

ScopedPerformanceTimer::ScopedPerformanceTimer (const char* label) noexcept
    : enabled_ (performanceTraceEnabled()),
      label_ (label == nullptr ? std::string_view {} : std::string_view { label }),
      start_ (enabled_ ? Clock::now() : Clock::time_point {})
{
}

ScopedPerformanceTimer::ScopedPerformanceTimer (std::string_view label) noexcept
    : enabled_ (performanceTraceEnabled()),
      label_ (label),
      start_ (enabled_ ? Clock::now() : Clock::time_point {})
{
}

ScopedPerformanceTimer::ScopedPerformanceTimer (std::string label)
    : enabled_ (performanceTraceEnabled()),
      ownedLabel_ (enabled_ ? std::move (label) : std::string {}),
      label_ (ownedLabel_),
      start_ (enabled_ ? Clock::now() : Clock::time_point {})
{
}

ScopedPerformanceTimer::ScopedPerformanceTimer (std::string_view prefix, std::string_view suffix)
    : enabled_ (performanceTraceEnabled()),
      start_ (enabled_ ? Clock::now() : Clock::time_point {})
{
    if (! enabled_)
        return;

    ownedLabel_.reserve (prefix.size() + suffix.size());
    ownedLabel_.append (prefix);
    ownedLabel_.append (suffix);
    label_ = ownedLabel_;
}

ScopedPerformanceTimer::~ScopedPerformanceTimer()
{
    if (! enabled_)
        return;

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds> (Clock::now() - start_).count();
    writePerformanceTrace (label_, elapsed);
}
}
