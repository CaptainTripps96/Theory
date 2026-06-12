#include "core/sequencing/HarmonicContextResolver.h"

#include <algorithm>

namespace tsq::core::sequencing
{
namespace
{
void addBoundaryIfInside (std::vector<time::TickPosition>& boundaries, time::TickPosition boundary, const Region& range)
{
    if (boundary > range.start() && boundary < range.end())
        boundaries.push_back (boundary);
}

void addContextBoundaries (std::vector<time::TickPosition>& boundaries, const MusicalStructure& structure, const Region& range)
{
    for (const auto& region : structure.keyCenterRegions())
    {
        addBoundaryIfInside (boundaries, region.start(), range);
        addBoundaryIfInside (boundaries, region.end(), range);
    }

    for (const auto& region : structure.scaleModeRegions())
    {
        addBoundaryIfInside (boundaries, region.start(), range);
        addBoundaryIfInside (boundaries, region.end(), range);
    }
}
}

HarmonicContextResolver::HarmonicContextResolver (const MusicalStructure& structure)
    : structure_ (structure)
{
}

HarmonicContext HarmonicContextResolver::resolveAt (time::TickPosition position) const
{
    return HarmonicContext { structure_.keyCenterAt (position), structure_.scaleDefinitionNameAt (position) };
}

std::vector<HarmonicContextSegment> HarmonicContextResolver::resolveRange (Region range) const
{
    if (range.duration().ticks() == 0)
        return {};

    std::vector<time::TickPosition> boundaries { range.start(), range.end() };
    addContextBoundaries (boundaries, structure_, range);

    std::sort (boundaries.begin(), boundaries.end());
    boundaries.erase (std::unique (boundaries.begin(), boundaries.end()), boundaries.end());

    std::vector<HarmonicContextSegment> result;
    for (std::size_t index = 0; index + 1 < boundaries.size(); ++index)
    {
        const auto segmentRegion = Region { boundaries[index], boundaries[index + 1] };
        result.push_back (HarmonicContextSegment { segmentRegion, resolveAt (segmentRegion.start()) });
    }

    return result;
}
}
