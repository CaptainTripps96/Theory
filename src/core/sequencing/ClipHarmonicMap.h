#pragma once

#include "core/sequencing/HarmonicContextResolver.h"

#include <vector>

namespace tsq::core::sequencing
{
struct ClipHarmonicRegion
{
    Region region;
    HarmonicContext originalContext;
};

class ClipHarmonicMap
{
public:
    ClipHarmonicMap();
    explicit ClipHarmonicMap (HarmonicContext defaultContext);

    static ClipHarmonicMap snapshotFromProject (time::TickPosition clipStartInProject,
                                                time::TickDuration clipLength,
                                                const HarmonicContextResolver& resolver);

    const HarmonicContext& defaultContext() const noexcept;
    const std::vector<ClipHarmonicRegion>& regions() const noexcept;

    void addRegion (ClipHarmonicRegion region);
    HarmonicContext contextAt (time::TickPosition clipLocalPosition) const;
    std::vector<ClipHarmonicRegion> segmentsForRange (Region clipLocalRange) const;

private:
    HarmonicContext defaultContext_;
    std::vector<ClipHarmonicRegion> regions_;
};
}
