#include "core/music_theory/Chord.h"

#include <utility>

namespace tsq::core::music_theory
{
Chord::Chord (PitchClass root, ChordQuality quality, std::vector<PitchClass> pitchClasses)
    : root_ (root),
      quality_ (quality),
      pitchClasses_ (std::move (pitchClasses))
{
}

PitchClass Chord::root() const noexcept
{
    return root_;
}

ChordQuality Chord::quality() const noexcept
{
    return quality_;
}

const std::vector<PitchClass>& Chord::pitchClasses() const noexcept
{
    return pitchClasses_;
}
}
