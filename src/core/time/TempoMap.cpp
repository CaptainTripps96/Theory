#include "core/time/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tsq::core::time
{
namespace
{
constexpr double epsilon = 0.000000001;

double secondsForConstantTempoTicks (double ticks, double bpm) noexcept
{
    return ticks * 60.0 / (static_cast<double> (ticksPerQuarterNote) * bpm);
}

double secondsForLinearTempoTicks (double ticks, double segmentTicks, double startBpm, double endBpm) noexcept
{
    if (ticks == 0.0)
        return 0.0;

    if (segmentTicks == 0.0 || std::abs (endBpm - startBpm) < epsilon)
        return secondsForConstantTempoTicks (ticks, startBpm);

    const auto slope = (endBpm - startBpm) / segmentTicks;
    const auto bpmAtEnd = startBpm + slope * ticks;
    return 60.0 * std::log (bpmAtEnd / startBpm) / (static_cast<double> (ticksPerQuarterNote) * slope);
}

void validateNonNegativePosition (TickPosition position)
{
    if (position.ticks() < 0)
        throw std::invalid_argument ("Tempo map positions must be non-negative");
}
}

TempoMap::TempoMap()
    : TempoMap (Tempo {})
{
}

TempoMap::TempoMap (Tempo initialTempo)
    : nodes_ { TempoNode { TickPosition {}, initialTempo } }
{
}

void TempoMap::addNode (TickPosition position, Tempo tempo)
{
    validateNonNegativePosition (position);

    const auto existing = std::find_if (nodes_.begin(),
                                        nodes_.end(),
                                        [position] (const TempoNode& node)
                                        {
                                            return node.position == position;
                                        });

    if (existing != nodes_.end())
    {
        existing->tempo = tempo;
        return;
    }

    nodes_.push_back (TempoNode { position, tempo });
    std::sort (nodes_.begin(),
               nodes_.end(),
               [] (const TempoNode& lhs, const TempoNode& rhs)
               {
                   return lhs.position < rhs.position;
               });
}

const std::vector<TempoNode>& TempoMap::nodes() const noexcept
{
    return nodes_;
}

Tempo TempoMap::tempoAt (TickPosition position) const
{
    validateNonNegativePosition (position);

    if (nodes_.size() == 1 || position <= nodes_.front().position)
        return nodes_.front().tempo;

    for (size_t index = 0; index + 1 < nodes_.size(); ++index)
    {
        const auto& start = nodes_[index];
        const auto& end = nodes_[index + 1];

        if (position >= start.position && position < end.position)
        {
            const auto segmentTicks = static_cast<double> ((end.position - start.position).ticks());
            const auto offsetTicks = static_cast<double> ((position - start.position).ticks());
            const auto ratio = offsetTicks / segmentTicks;
            const auto bpm = start.tempo.bpm() + (end.tempo.bpm() - start.tempo.bpm()) * ratio;
            return Tempo { bpm };
        }
    }

    return nodes_.back().tempo;
}

double TempoMap::secondsAt (TickPosition position) const
{
    validateNonNegativePosition (position);

    if (position.ticks() == 0)
        return 0.0;

    double seconds = 0.0;

    for (size_t index = 0; index + 1 < nodes_.size(); ++index)
    {
        const auto& start = nodes_[index];
        const auto& end = nodes_[index + 1];

        if (position <= start.position)
            return seconds;

        const auto segmentTicks = static_cast<double> ((end.position - start.position).ticks());
        const auto targetTicks = std::min (static_cast<double> ((position - start.position).ticks()), segmentTicks);
        seconds += secondsForLinearTempoTicks (targetTicks, segmentTicks, start.tempo.bpm(), end.tempo.bpm());

        if (position < end.position)
            return seconds;
    }

    const auto& finalNode = nodes_.back();

    if (position > finalNode.position)
        seconds += finalNode.tempo.ticksToSeconds (position - finalNode.position);

    return seconds;
}

double TempoMap::durationToSeconds (TickPosition start, TickDuration duration) const
{
    const auto end = start + duration;
    return secondsAt (end) - secondsAt (start);
}

TickPosition TempoMap::tickAtSeconds (double seconds) const
{
    if (! std::isfinite (seconds))
        throw std::invalid_argument ("Seconds must be finite");

    if (seconds <= 0.0)
        return TickPosition {};

    std::int64_t low = 0;
    std::int64_t high = ticksPerQuarterNote;

    while (secondsAt (TickPosition::fromTicks (high)) < seconds)
        high *= 2;

    while (low < high)
    {
        const auto mid = low + (high - low) / 2;

        if (secondsAt (TickPosition::fromTicks (mid)) < seconds)
            low = mid + 1;
        else
            high = mid;
    }

    return TickPosition::fromTicks (low);
}
}
