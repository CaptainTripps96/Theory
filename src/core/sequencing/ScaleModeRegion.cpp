#include "core/sequencing/ScaleModeRegion.h"

#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
ScaleModeRegion::ScaleModeRegion (Region region, std::string scaleDefinitionName)
    : region_ (std::move (region)),
      scaleDefinitionName_ (std::move (scaleDefinitionName))
{
    if (scaleDefinitionName_.empty())
        throw std::invalid_argument ("ScaleModeRegion requires a scale definition name");
}

const Region& ScaleModeRegion::region() const noexcept
{
    return region_;
}

time::TickPosition ScaleModeRegion::start() const noexcept
{
    return region_.start();
}

time::TickPosition ScaleModeRegion::end() const noexcept
{
    return region_.end();
}

const std::string& ScaleModeRegion::scaleDefinitionName() const noexcept
{
    return scaleDefinitionName_;
}
}
