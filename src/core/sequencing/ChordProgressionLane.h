#pragma once

#include "core/sequencing/ChordRegion.h"

#include <vector>

namespace tsq::core::sequencing
{
class ChordProgressionLane
{
public:
    void addRegion (ChordRegion region);
    bool removeRegion (const ChordRegion& region);

    const std::vector<ChordRegion>& regions() const noexcept;

private:
    std::vector<ChordRegion> regions_;
};
}
