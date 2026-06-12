#include "core/time/Tick.h"

#include <cmath>
#include <stdexcept>

namespace tsq::core::time
{
TickDuration durationFromQuarterNotes (double quarterNotes)
{
    if (! std::isfinite (quarterNotes))
        throw std::invalid_argument ("Quarter-note duration must be finite");

    return TickDuration::fromTicks (std::llround (quarterNotes * static_cast<double> (ticksPerQuarterNote)));
}

double quarterNotesFromDuration (TickDuration duration) noexcept
{
    return static_cast<double> (duration.ticks()) / static_cast<double> (ticksPerQuarterNote);
}
}
