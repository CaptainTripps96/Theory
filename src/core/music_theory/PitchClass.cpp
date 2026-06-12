#include "core/music_theory/PitchClass.h"

namespace tsq::core::music_theory
{
bool hasNaturalLetterName (PitchClass pitchClass) noexcept
{
    switch (pitchClass.semitonesFromC())
    {
        case 0:
        case 2:
        case 4:
        case 5:
        case 7:
        case 9:
        case 11:
            return true;

        default:
            return false;
    }
}
}
