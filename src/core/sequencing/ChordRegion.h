#pragma once

#include "core/music_theory/ChordQuality.h"
#include "core/music_theory/PitchClass.h"
#include "core/sequencing/Region.h"

#include <string>
#include <vector>

namespace tsq::core::sequencing
{
class ChordRegion
{
public:
    ChordRegion (Region region,
                 music_theory::PitchClass root,
                 music_theory::ChordQuality quality,
                 std::vector<music_theory::PitchClass> chordTones,
                 std::string chordName);

    const Region& region() const noexcept;
    time::TickPosition start() const noexcept;
    time::TickPosition end() const noexcept;
    music_theory::PitchClass root() const noexcept;
    music_theory::ChordQuality quality() const noexcept;
    const std::vector<music_theory::PitchClass>& chordTones() const noexcept;
    const std::string& chordName() const noexcept;

private:
    Region region_;
    music_theory::PitchClass root_ {};
    music_theory::ChordQuality quality_ = music_theory::ChordQuality::major;
    std::vector<music_theory::PitchClass> chordTones_;
    std::string chordName_;
};
}
