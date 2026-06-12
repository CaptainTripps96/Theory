#pragma once

#include "core/time/Tick.h"

namespace tsq::core::time
{
class TimeSignature
{
public:
    TimeSignature (int numerator = 4, int denominator = 4);

    int numerator() const noexcept;
    int denominator() const noexcept;

    TickDuration beatDuration() const noexcept;
    TickDuration barDuration() const noexcept;

private:
    int numerator_ = 4;
    int denominator_ = 4;
};
}
