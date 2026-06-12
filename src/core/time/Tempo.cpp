#include "core/time/Tempo.h"

#include <cmath>
#include <stdexcept>

namespace tsq::core::time
{
Tempo::Tempo (double beatsPerMinute)
    : bpm_ (beatsPerMinute)
{
    if (! std::isfinite (bpm_) || bpm_ <= 0.0)
        throw std::invalid_argument ("Tempo must be a positive finite BPM value");
}

double Tempo::bpm() const noexcept
{
    return bpm_;
}

double Tempo::secondsPerQuarterNote() const noexcept
{
    return 60.0 / bpm_;
}

double Tempo::ticksToSeconds (TickDuration duration) const noexcept
{
    return static_cast<double> (duration.ticks()) * secondsPerQuarterNote() / static_cast<double> (ticksPerQuarterNote);
}

TickDuration Tempo::secondsToTicks (double seconds) const
{
    if (! std::isfinite (seconds))
        throw std::invalid_argument ("Seconds must be finite");

    const auto ticks = std::llround (seconds * bpm_ * static_cast<double> (ticksPerQuarterNote) / 60.0);
    return TickDuration::fromTicks (ticks);
}
}
