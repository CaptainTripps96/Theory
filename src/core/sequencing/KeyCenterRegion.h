#pragma once

#include "core/music_theory/PitchClass.h"
#include "core/sequencing/Region.h"

namespace tsq::core::sequencing
{
class KeyCenterRegion
{
public:
    KeyCenterRegion (Region region, music_theory::PitchClass pitchClass);

    const Region& region() const noexcept;
    time::TickPosition start() const noexcept;
    time::TickPosition end() const noexcept;
    music_theory::PitchClass pitchClass() const noexcept;

private:
    Region region_;
    music_theory::PitchClass pitchClass_;
};
}
