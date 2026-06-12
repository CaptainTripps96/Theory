#include "core/sequencing/ChordRegion.h"

#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
ChordRegion::ChordRegion (Region region,
                           music_theory::PitchClass root,
                           music_theory::ChordQuality quality,
                           std::vector<music_theory::PitchClass> chordTones,
                           std::string chordName)
    : region_ (std::move (region)),
      root_ (root),
      quality_ (quality),
      chordTones_ (std::move (chordTones)),
      chordName_ (std::move (chordName))
{
    if (chordTones_.empty())
        throw std::invalid_argument ("ChordRegion requires at least one chord tone");

    if (chordName_.empty())
        throw std::invalid_argument ("ChordRegion requires a chord name");
}

const Region& ChordRegion::region() const noexcept
{
    return region_;
}

time::TickPosition ChordRegion::start() const noexcept
{
    return region_.start();
}

time::TickPosition ChordRegion::end() const noexcept
{
    return region_.end();
}

music_theory::PitchClass ChordRegion::root() const noexcept
{
    return root_;
}

music_theory::ChordQuality ChordRegion::quality() const noexcept
{
    return quality_;
}

const std::vector<music_theory::PitchClass>& ChordRegion::chordTones() const noexcept
{
    return chordTones_;
}

const std::string& ChordRegion::chordName() const noexcept
{
    return chordName_;
}
}
