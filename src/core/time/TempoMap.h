#pragma once

#include "core/time/Tempo.h"

#include <vector>

namespace tsq::core::time
{
struct TempoNode
{
    TickPosition position {};
    Tempo tempo {};
};

class TempoMap
{
public:
    TempoMap();
    explicit TempoMap (Tempo initialTempo);

    void addNode (TickPosition position, Tempo tempo);

    const std::vector<TempoNode>& nodes() const noexcept;
    Tempo tempoAt (TickPosition position) const;
    double secondsAt (TickPosition position) const;
    double durationToSeconds (TickPosition start, TickDuration duration) const;
    TickPosition tickAtSeconds (double seconds) const;

private:
    std::vector<TempoNode> nodes_;
};
}
