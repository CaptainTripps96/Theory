#include "core/time/TimeSignatureMap.h"

#include <algorithm>
#include <stdexcept>

namespace tsq::core::time
{
namespace
{
void validateNonNegativePosition (TickPosition position)
{
    if (position.ticks() < 0)
        throw std::invalid_argument ("Time signature map positions must be non-negative");
}
}

TimeSignatureMap::TimeSignatureMap()
    : TimeSignatureMap (TimeSignature {})
{
}

TimeSignatureMap::TimeSignatureMap (TimeSignature initialTimeSignature)
    : markers_ { TimeSignatureMarker { TickPosition {}, initialTimeSignature } }
{
}

void TimeSignatureMap::addMarker (TickPosition position, TimeSignature timeSignature)
{
    validateNonNegativePosition (position);

    const auto existing = std::find_if (markers_.begin(),
                                        markers_.end(),
                                        [position] (const TimeSignatureMarker& marker)
                                        {
                                            return marker.position == position;
                                        });

    if (existing != markers_.end())
    {
        existing->timeSignature = timeSignature;
        return;
    }

    markers_.push_back (TimeSignatureMarker { position, timeSignature });
    std::sort (markers_.begin(),
               markers_.end(),
               [] (const TimeSignatureMarker& lhs, const TimeSignatureMarker& rhs)
               {
                   return lhs.position < rhs.position;
               });
}

const std::vector<TimeSignatureMarker>& TimeSignatureMap::markers() const noexcept
{
    return markers_;
}

TimeSignature TimeSignatureMap::timeSignatureAt (TickPosition position) const
{
    validateNonNegativePosition (position);

    auto selected = markers_.front().timeSignature;

    for (const auto& marker : markers_)
    {
        if (marker.position > position)
            break;

        selected = marker.timeSignature;
    }

    return selected;
}

TickDuration TimeSignatureMap::barDurationAt (TickPosition position) const
{
    return timeSignatureAt (position).barDuration();
}

TickPosition TimeSignatureMap::tickAtBar (int bar) const
{
    if (bar < 1)
        throw std::invalid_argument ("Bar numbers are 1-based and must be positive");

    auto tick = TickPosition {};

    for (int currentBar = 1; currentBar < bar; ++currentBar)
        tick = tick + timeSignatureAt (tick).barDuration();

    return tick;
}

TickPosition TimeSignatureMap::toTicks (BarBeatPosition position) const
{
    if (position.bar < 1)
        throw std::invalid_argument ("Bar numbers are 1-based and must be positive");

    const auto barStart = tickAtBar (position.bar);
    const auto timeSignature = timeSignatureAt (barStart);

    if (position.beat < 1 || position.beat > timeSignature.numerator())
        throw std::invalid_argument ("Beat is outside the active time signature");

    if (position.tickOffset.ticks() < 0 || position.tickOffset >= timeSignature.beatDuration())
        throw std::invalid_argument ("Beat tick offset is outside the active beat");

    return barStart + (timeSignature.beatDuration() * (position.beat - 1)) + position.tickOffset;
}

BarBeatPosition TimeSignatureMap::fromTicks (TickPosition position) const
{
    validateNonNegativePosition (position);

    auto barStart = TickPosition {};
    int bar = 1;

    while (true)
    {
        const auto timeSignature = timeSignatureAt (barStart);
        const auto nextBarStart = barStart + timeSignature.barDuration();

        if (position < nextBarStart)
        {
            const auto ticksIntoBar = position - barStart;
            const auto beatTicks = timeSignature.beatDuration().ticks();
            const auto beatIndex = static_cast<int> (ticksIntoBar.ticks() / beatTicks);
            const auto offset = TickDuration::fromTicks (ticksIntoBar.ticks() % beatTicks);
            return BarBeatPosition { bar, beatIndex + 1, offset };
        }

        barStart = nextBarStart;
        ++bar;
    }
}
}
