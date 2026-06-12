#include "core/sequencing/ChordProgressionLane.h"

#include <algorithm>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
bool samePitchClasses (const std::vector<music_theory::PitchClass>& lhs,
                       const std::vector<music_theory::PitchClass>& rhs)
{
    return lhs == rhs;
}
}

void ChordProgressionLane::addRegion (ChordRegion region)
{
    regions_.push_back (std::move (region));
    std::stable_sort (regions_.begin(), regions_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.start() < rhs.start();
    });
}

bool ChordProgressionLane::removeRegion (const ChordRegion& region)
{
    const auto match = std::find_if (regions_.begin(), regions_.end(), [&region] (const auto& candidate) {
        return candidate.start() == region.start()
            && candidate.end() == region.end()
            && candidate.root() == region.root()
            && candidate.quality() == region.quality()
            && candidate.chordName() == region.chordName()
            && samePitchClasses (candidate.chordTones(), region.chordTones());
    });

    if (match == regions_.end())
        return false;

    regions_.erase (match);
    return true;
}

const std::vector<ChordRegion>& ChordProgressionLane::regions() const noexcept
{
    return regions_;
}
}
