#pragma once

#include "core/time/Tick.h"

namespace tsq::core::sequencing
{
class Region
{
public:
    Region (time::TickPosition start, time::TickPosition end);

    time::TickPosition start() const noexcept;
    time::TickPosition end() const noexcept;
    time::TickDuration duration() const noexcept;

    bool contains (time::TickPosition position) const noexcept;
    bool intersects (const Region& other) const noexcept;

private:
    time::TickPosition start_ {};
    time::TickPosition end_ {};
};
}
