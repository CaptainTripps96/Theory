#pragma once

#include "core/music_theory/ChordQuality.h"
#include "core/music_theory/PitchClass.h"

#include <vector>

namespace tsq::core::music_theory
{
class Chord
{
public:
    Chord (PitchClass root, ChordQuality quality, std::vector<PitchClass> pitchClasses);

    PitchClass root() const noexcept;
    ChordQuality quality() const noexcept;
    const std::vector<PitchClass>& pitchClasses() const noexcept;

private:
    PitchClass root_ {};
    ChordQuality quality_ = ChordQuality::major;
    std::vector<PitchClass> pitchClasses_;
};
}
