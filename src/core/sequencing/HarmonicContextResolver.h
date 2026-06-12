#pragma once

#include "core/sequencing/HarmonicContext.h"
#include "core/sequencing/MusicalStructure.h"

#include <vector>

namespace tsq::core::sequencing
{
struct HarmonicContextSegment
{
    Region region;
    HarmonicContext context;
};

class HarmonicContextResolver
{
public:
    explicit HarmonicContextResolver (const MusicalStructure& structure);

    HarmonicContext resolveAt (time::TickPosition position) const;
    std::vector<HarmonicContextSegment> resolveRange (Region range) const;

private:
    const MusicalStructure& structure_;
};
}
