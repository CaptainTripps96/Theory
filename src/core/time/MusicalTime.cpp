#include "core/time/MusicalTime.h"

namespace tsq::core::time
{
bool isValid (BarBeatPosition position) noexcept
{
    return position.bar > 0 && position.beat > 0 && position.tickOffset.ticks() >= 0;
}

bool isValid (SubdivisionPosition position) noexcept
{
    return position.bar > 0
           && position.beat > 0
           && position.subdivisionsPerBeat > 0
           && position.subdivision >= 0
           && position.subdivision < position.subdivisionsPerBeat;
}
}
