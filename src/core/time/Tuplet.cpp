#include "core/time/Tuplet.h"

#include <cmath>
#include <stdexcept>

namespace tsq::core::time
{
Tuplet::Tuplet (TickDuration baseUnit, int notes, int inTimeOf)
    : baseUnit_ (baseUnit),
      notes_ (notes),
      inTimeOf_ (inTimeOf)
{
    if (baseUnit_.ticks() <= 0)
        throw std::invalid_argument ("Tuplet base unit must be positive");

    if (notes_ <= 0)
        throw std::invalid_argument ("Tuplet note count must be positive");

    if (inTimeOf_ <= 0)
        throw std::invalid_argument ("Tuplet in-time-of count must be positive");
}

TickDuration Tuplet::baseUnit() const noexcept
{
    return baseUnit_;
}

int Tuplet::notes() const noexcept
{
    return notes_;
}

int Tuplet::inTimeOf() const noexcept
{
    return inTimeOf_;
}

TickDuration Tuplet::totalDuration() const noexcept
{
    return baseUnit_ * inTimeOf_;
}

double Tuplet::noteDurationTicksExact() const noexcept
{
    return static_cast<double> (totalDuration().ticks()) / static_cast<double> (notes_);
}

TickDuration Tuplet::noteDuration() const noexcept
{
    return TickDuration::fromTicks (std::llround (noteDurationTicksExact()));
}
}
