#pragma once

#include "core/time/Tick.h"

namespace tsq::core::time
{
class Tempo
{
public:
    explicit Tempo (double beatsPerMinute = 120.0);

    double bpm() const noexcept;
    double secondsPerQuarterNote() const noexcept;
    double ticksToSeconds (TickDuration duration) const noexcept;
    TickDuration secondsToTicks (double seconds) const;

private:
    double bpm_ = 120.0;
};
}
