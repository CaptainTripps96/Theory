#pragma once

#include "core/time/MusicalTime.h"
#include "core/time/TimeSignature.h"

#include <vector>

namespace tsq::core::time
{
struct TimeSignatureMarker
{
    TickPosition position {};
    TimeSignature timeSignature {};
};

class TimeSignatureMap
{
public:
    TimeSignatureMap();
    explicit TimeSignatureMap (TimeSignature initialTimeSignature);

    void addMarker (TickPosition position, TimeSignature timeSignature);

    const std::vector<TimeSignatureMarker>& markers() const noexcept;
    TimeSignature timeSignatureAt (TickPosition position) const;
    TickDuration barDurationAt (TickPosition position) const;
    TickPosition tickAtBar (int bar) const;
    TickPosition toTicks (BarBeatPosition position) const;
    BarBeatPosition fromTicks (TickPosition position) const;

private:
    std::vector<TimeSignatureMarker> markers_;
};
}
