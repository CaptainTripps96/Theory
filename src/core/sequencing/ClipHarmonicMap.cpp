#include "core/sequencing/ClipHarmonicMap.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
void addBoundaryIfInside (std::vector<time::TickPosition>& boundaries, time::TickPosition boundary, const Region& range)
{
    if (boundary > range.start() && boundary < range.end())
        boundaries.push_back (boundary);
}

time::TickPosition projectToClipLocal (time::TickPosition projectPosition, time::TickPosition clipStartInProject)
{
    return time::TickPosition {} + (projectPosition - clipStartInProject);
}
}

ClipHarmonicMap::ClipHarmonicMap()
    : defaultContext_ (music_theory::PitchClass::c(), "Major")
{
}

ClipHarmonicMap::ClipHarmonicMap (HarmonicContext defaultContext)
    : defaultContext_ (std::move (defaultContext))
{
}

ClipHarmonicMap ClipHarmonicMap::snapshotFromProject (time::TickPosition clipStartInProject,
                                                      time::TickDuration clipLength,
                                                      const HarmonicContextResolver& resolver)
{
    if (clipLength.ticks() < 0)
        throw std::invalid_argument ("Clip harmonic snapshot length must not be negative");

    ClipHarmonicMap result;
    if (clipLength.ticks() == 0)
        return result;

    const auto projectRegion = Region { clipStartInProject, clipStartInProject + clipLength };
    for (const auto& segment : resolver.resolveRange (projectRegion))
    {
        result.addRegion (ClipHarmonicRegion {
            Region {
                projectToClipLocal (segment.region.start(), clipStartInProject),
                projectToClipLocal (segment.region.end(), clipStartInProject)
            },
            segment.context
        });
    }

    return result;
}

const HarmonicContext& ClipHarmonicMap::defaultContext() const noexcept
{
    return defaultContext_;
}

const std::vector<ClipHarmonicRegion>& ClipHarmonicMap::regions() const noexcept
{
    return regions_;
}

void ClipHarmonicMap::addRegion (ClipHarmonicRegion region)
{
    if (region.region.start().ticks() < 0)
        throw std::invalid_argument ("Clip harmonic region start tick must not be negative");

    for (const auto& existingRegion : regions_)
    {
        if (existingRegion.region.intersects (region.region))
            throw std::invalid_argument ("Clip harmonic regions must not overlap");
    }

    regions_.push_back (std::move (region));
    std::stable_sort (regions_.begin(), regions_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.region.start() < rhs.region.start();
    });
}

HarmonicContext ClipHarmonicMap::contextAt (time::TickPosition clipLocalPosition) const
{
    if (clipLocalPosition.ticks() < 0)
        throw std::invalid_argument ("Clip-local harmonic context position must not be negative");

    for (const auto& region : regions_)
    {
        if (region.region.contains (clipLocalPosition))
            return region.originalContext;
    }

    return defaultContext_;
}

std::vector<ClipHarmonicRegion> ClipHarmonicMap::segmentsForRange (Region clipLocalRange) const
{
    if (clipLocalRange.duration().ticks() == 0)
        return {};

    std::vector<time::TickPosition> boundaries { clipLocalRange.start(), clipLocalRange.end() };
    for (const auto& region : regions_)
    {
        addBoundaryIfInside (boundaries, region.region.start(), clipLocalRange);
        addBoundaryIfInside (boundaries, region.region.end(), clipLocalRange);
    }

    std::sort (boundaries.begin(), boundaries.end());
    boundaries.erase (std::unique (boundaries.begin(), boundaries.end()), boundaries.end());

    std::vector<ClipHarmonicRegion> result;
    for (std::size_t index = 0; index + 1 < boundaries.size(); ++index)
    {
        const auto segmentRegion = Region { boundaries[index], boundaries[index + 1] };
        result.push_back (ClipHarmonicRegion { segmentRegion, contextAt (segmentRegion.start()) });
    }

    return result;
}
}
