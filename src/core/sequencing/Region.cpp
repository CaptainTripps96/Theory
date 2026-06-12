#include "core/sequencing/Region.h"

#include <stdexcept>

namespace tsq::core::sequencing
{
Region::Region (time::TickPosition start, time::TickPosition end)
    : start_ (start),
      end_ (end)
{
    if (end_ < start_)
        throw std::invalid_argument ("Region end must not be before start");
}

time::TickPosition Region::start() const noexcept
{
    return start_;
}

time::TickPosition Region::end() const noexcept
{
    return end_;
}

time::TickDuration Region::duration() const noexcept
{
    return end_ - start_;
}

bool Region::contains (time::TickPosition position) const noexcept
{
    return position >= start_ && position < end_;
}

bool Region::intersects (const Region& other) const noexcept
{
    return start_ < other.end_ && other.start_ < end_;
}
}
