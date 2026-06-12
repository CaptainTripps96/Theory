#pragma once

#include "core/sequencing/Region.h"

#include <string>

namespace tsq::core::sequencing
{
class ScaleModeRegion
{
public:
    ScaleModeRegion (Region region, std::string scaleDefinitionName);

    const Region& region() const noexcept;
    time::TickPosition start() const noexcept;
    time::TickPosition end() const noexcept;
    const std::string& scaleDefinitionName() const noexcept;

private:
    Region region_;
    std::string scaleDefinitionName_;
};
}
