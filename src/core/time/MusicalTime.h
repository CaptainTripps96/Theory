#pragma once

#include "core/time/Tick.h"

namespace tsq::core::time
{
struct BarBeatPosition
{
    int bar = 1;
    int beat = 1;
    TickDuration tickOffset {};
};

struct SubdivisionPosition
{
    int bar = 1;
    int beat = 1;
    int subdivision = 0;
    int subdivisionsPerBeat = 1;
};

bool isValid (BarBeatPosition position) noexcept;
bool isValid (SubdivisionPosition position) noexcept;
}
