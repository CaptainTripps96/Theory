#include "core/sequencing/MusicalStructure.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
template <typename RegionType>
void sortByStart (std::vector<RegionType>& regions)
{
    std::stable_sort (regions.begin(), regions.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.start() < rhs.start();
    });
}

template <typename RegionType>
const RegionType* latestContainingRegion (const std::vector<RegionType>& regions, time::TickPosition position)
{
    const RegionType* result = nullptr;

    for (const auto& region : regions)
    {
        if (! region.region().contains (position))
            continue;

        if (result == nullptr || region.start() >= result->start())
            result = &region;
    }

    return result;
}
}

MusicalStructure::MusicalStructure() = default;

MusicalStructure::MusicalStructure (music_theory::PitchClass defaultKeyCenter, std::string defaultScaleDefinitionName)
    : defaultKeyCenter_ (defaultKeyCenter),
      defaultScaleDefinitionName_ (std::move (defaultScaleDefinitionName))
{
    if (defaultScaleDefinitionName_.empty())
        throw std::invalid_argument ("MusicalStructure requires a default scale definition name");
}

music_theory::PitchClass MusicalStructure::defaultKeyCenter() const noexcept
{
    return defaultKeyCenter_;
}

const std::string& MusicalStructure::defaultScaleDefinitionName() const noexcept
{
    return defaultScaleDefinitionName_;
}

void MusicalStructure::addKeyCenterRegion (KeyCenterRegion region)
{
    keyCenterRegions_.push_back (std::move (region));
    sortByStart (keyCenterRegions_);
}

void MusicalStructure::addScaleModeRegion (ScaleModeRegion region)
{
    scaleModeRegions_.push_back (std::move (region));
    sortByStart (scaleModeRegions_);
}

void MusicalStructure::addChordRegion (ChordRegion region)
{
    chordProgressionLane_.addRegion (std::move (region));
}

bool MusicalStructure::removeKeyCenterRegion (const KeyCenterRegion& region)
{
    const auto match = std::find_if (keyCenterRegions_.begin(), keyCenterRegions_.end(), [&region] (const auto& candidate) {
        return candidate.start() == region.start()
            && candidate.end() == region.end()
            && candidate.pitchClass() == region.pitchClass();
    });

    if (match == keyCenterRegions_.end())
        return false;

    keyCenterRegions_.erase (match);
    return true;
}

bool MusicalStructure::removeScaleModeRegion (const ScaleModeRegion& region)
{
    const auto match = std::find_if (scaleModeRegions_.begin(), scaleModeRegions_.end(), [&region] (const auto& candidate) {
        return candidate.start() == region.start()
            && candidate.end() == region.end()
            && candidate.scaleDefinitionName() == region.scaleDefinitionName();
    });

    if (match == scaleModeRegions_.end())
        return false;

    scaleModeRegions_.erase (match);
    return true;
}

bool MusicalStructure::removeChordRegion (const ChordRegion& region)
{
    return chordProgressionLane_.removeRegion (region);
}

const std::vector<KeyCenterRegion>& MusicalStructure::keyCenterRegions() const noexcept
{
    return keyCenterRegions_;
}

const std::vector<ScaleModeRegion>& MusicalStructure::scaleModeRegions() const noexcept
{
    return scaleModeRegions_;
}

ChordProgressionLane& MusicalStructure::chordProgressionLane() noexcept
{
    return chordProgressionLane_;
}

const ChordProgressionLane& MusicalStructure::chordProgressionLane() const noexcept
{
    return chordProgressionLane_;
}

const std::vector<ChordRegion>& MusicalStructure::chordRegions() const noexcept
{
    return chordProgressionLane_.regions();
}

music_theory::PitchClass MusicalStructure::keyCenterAt (time::TickPosition position) const noexcept
{
    if (const auto* region = latestContainingRegion (keyCenterRegions_, position))
        return region->pitchClass();

    return defaultKeyCenter_;
}

std::string MusicalStructure::scaleDefinitionNameAt (time::TickPosition position) const
{
    if (const auto* region = latestContainingRegion (scaleModeRegions_, position))
        return region->scaleDefinitionName();

    return defaultScaleDefinitionName_;
}
}
