#pragma once

#include "core/sequencing/Region.h"

#include <vector>

namespace tsq::core::sequencing
{
class ClipLoop
{
public:
    ClipLoop() noexcept = default;
    explicit ClipLoop (time::TickDuration loopDuration);

    static ClipLoop disabled() noexcept;
    static ClipLoop enabled (time::TickDuration loopDuration);

    bool isEnabled() const noexcept;
    time::TickDuration loopDuration() const noexcept;
    std::vector<Region> repetitionsForLength (time::TickDuration clipLength) const;

private:
    bool enabled_ = false;
    time::TickDuration loopDuration_ {};
};
}
