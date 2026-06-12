#include "core/sequencing/KeyCenterRegion.h"

#include <utility>

namespace tsq::core::sequencing
{
KeyCenterRegion::KeyCenterRegion (Region region, music_theory::PitchClass pitchClass)
    : region_ (std::move (region)),
      pitchClass_ (pitchClass)
{
}

const Region& KeyCenterRegion::region() const noexcept
{
    return region_;
}

time::TickPosition KeyCenterRegion::start() const noexcept
{
    return region_.start();
}

time::TickPosition KeyCenterRegion::end() const noexcept
{
    return region_.end();
}

music_theory::PitchClass KeyCenterRegion::pitchClass() const noexcept
{
    return pitchClass_;
}
}
