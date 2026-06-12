#include "core/sequencing/ClipLoop.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace tsq::core::sequencing
{
ClipLoop::ClipLoop (time::TickDuration loopDuration)
    : enabled_ (true),
      loopDuration_ (loopDuration)
{
    if (loopDuration_.ticks() <= 0)
        throw std::invalid_argument ("ClipLoop duration must be positive");
}

ClipLoop ClipLoop::disabled() noexcept
{
    return ClipLoop {};
}

ClipLoop ClipLoop::enabled (time::TickDuration loopDuration)
{
    return ClipLoop { loopDuration };
}

bool ClipLoop::isEnabled() const noexcept
{
    return enabled_;
}

time::TickDuration ClipLoop::loopDuration() const noexcept
{
    return loopDuration_;
}

std::vector<Region> ClipLoop::repetitionsForLength (time::TickDuration clipLength) const
{
    if (clipLength.ticks() < 0)
        throw std::invalid_argument ("Clip length must not be negative");

    if (clipLength.ticks() == 0)
        return {};

    if (! enabled_)
        return { Region { time::TickPosition {}, time::TickPosition {} + clipLength } };

    std::vector<Region> repetitions;

    for (auto start = std::int64_t { 0 }; start < clipLength.ticks(); start += loopDuration_.ticks())
    {
        const auto end = std::min (start + loopDuration_.ticks(), clipLength.ticks());
        repetitions.emplace_back (time::TickPosition::fromTicks (start), time::TickPosition::fromTicks (end));
    }

    return repetitions;
}
}
