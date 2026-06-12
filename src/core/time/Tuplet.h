#pragma once

#include "core/time/Tick.h"

namespace tsq::core::time
{
class Tuplet
{
public:
    Tuplet (TickDuration baseUnit, int notes, int inTimeOf);

    TickDuration baseUnit() const noexcept;
    int notes() const noexcept;
    int inTimeOf() const noexcept;
    TickDuration totalDuration() const noexcept;
    double noteDurationTicksExact() const noexcept;
    TickDuration noteDuration() const noexcept;

private:
    TickDuration baseUnit_;
    int notes_ = 1;
    int inTimeOf_ = 1;
};
}
