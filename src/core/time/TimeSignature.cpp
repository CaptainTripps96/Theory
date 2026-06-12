#include "core/time/TimeSignature.h"

#include <stdexcept>

namespace tsq::core::time
{
namespace
{
bool isPowerOfTwo (int value) noexcept
{
    return value > 0 && (value & (value - 1)) == 0;
}
}

TimeSignature::TimeSignature (int numerator, int denominator)
    : numerator_ (numerator),
      denominator_ (denominator)
{
    if (numerator_ <= 0)
        throw std::invalid_argument ("Time signature numerator must be positive");

    if (! isPowerOfTwo (denominator_))
        throw std::invalid_argument ("Time signature denominator must be a positive power of two");
}

int TimeSignature::numerator() const noexcept
{
    return numerator_;
}

int TimeSignature::denominator() const noexcept
{
    return denominator_;
}

TickDuration TimeSignature::beatDuration() const noexcept
{
    return TickDuration::fromTicks ((ticksPerQuarterNote * 4) / denominator_);
}

TickDuration TimeSignature::barDuration() const noexcept
{
    return beatDuration() * numerator_;
}
}
