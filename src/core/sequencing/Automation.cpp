#include "core/sequencing/Automation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
double normalized (double value)
{
    if (std::isnan (value) || value < 0.0 || value > 1.0)
        throw std::invalid_argument ("Automation values must be normalized between 0 and 1");

    return value;
}

void requireTrackId (const std::string& trackId)
{
    if (trackId.empty())
        throw std::invalid_argument ("Automation target requires a track ID");
}
}

AutomationTarget AutomationTarget::trackVolume (std::string trackId)
{
    requireTrackId (trackId);
    AutomationTarget target;
    target.kind = AutomationTargetKind::trackVolume;
    target.trackId = std::move (trackId);
    return target;
}

AutomationTarget AutomationTarget::trackPan (std::string trackId)
{
    requireTrackId (trackId);
    AutomationTarget target;
    target.kind = AutomationTargetKind::trackPan;
    target.trackId = std::move (trackId);
    return target;
}

AutomationTarget AutomationTarget::trackMute (std::string trackId)
{
    requireTrackId (trackId);
    AutomationTarget target;
    target.kind = AutomationTargetKind::trackMute;
    target.trackId = std::move (trackId);
    return target;
}

AutomationTarget AutomationTarget::sendLevel (std::string trackId, std::string returnTrackId)
{
    requireTrackId (trackId);
    if (returnTrackId.empty())
        throw std::invalid_argument ("Send automation requires a return track ID");

    AutomationTarget target;
    target.kind = AutomationTargetKind::sendLevel;
    target.trackId = std::move (trackId);
    target.sendTargetTrackId = std::move (returnTrackId);
    return target;
}

AutomationTarget AutomationTarget::deviceBypass (std::string trackId, DeviceSlotId deviceSlotId)
{
    requireTrackId (trackId);
    if (deviceSlotId.empty())
        throw std::invalid_argument ("Device bypass automation requires a device slot ID");

    AutomationTarget target;
    target.kind = AutomationTargetKind::deviceBypass;
    target.trackId = std::move (trackId);
    target.deviceSlotId = std::move (deviceSlotId);
    return target;
}

AutomationTarget AutomationTarget::pluginParameter (std::string trackId, DeviceSlotId deviceSlotId, std::string parameterId)
{
    requireTrackId (trackId);
    if (deviceSlotId.empty())
        throw std::invalid_argument ("Plugin parameter automation requires a device slot ID");
    if (parameterId.empty())
        throw std::invalid_argument ("Plugin parameter automation requires a parameter ID");

    AutomationTarget target;
    target.kind = AutomationTargetKind::pluginParameter;
    target.trackId = std::move (trackId);
    target.deviceSlotId = std::move (deviceSlotId);
    target.pluginParameterId = std::move (parameterId);
    return target;
}

std::string AutomationTarget::stableId() const
{
    switch (kind)
    {
        case AutomationTargetKind::trackVolume: return trackId + ":volume";
        case AutomationTargetKind::trackPan: return trackId + ":pan";
        case AutomationTargetKind::trackMute: return trackId + ":mute";
        case AutomationTargetKind::sendLevel: return trackId + ":send:" + sendTargetTrackId;
        case AutomationTargetKind::deviceBypass: return trackId + ":device:" + deviceSlotId.value + ":bypass";
        case AutomationTargetKind::pluginParameter: return trackId + ":device:" + deviceSlotId.value + ":parameter:" + pluginParameterId;
    }

    return trackId;
}

bool AutomationTarget::isValid() const noexcept
{
    if (trackId.empty())
        return false;

    switch (kind)
    {
        case AutomationTargetKind::trackVolume:
        case AutomationTargetKind::trackPan:
        case AutomationTargetKind::trackMute:
            return true;
        case AutomationTargetKind::sendLevel:
            return ! sendTargetTrackId.empty();
        case AutomationTargetKind::deviceBypass:
            return ! deviceSlotId.empty();
        case AutomationTargetKind::pluginParameter:
            return ! deviceSlotId.empty() && ! pluginParameterId.empty();
    }

    return false;
}

bool operator== (const AutomationTarget& lhs, const AutomationTarget& rhs) noexcept
{
    return lhs.kind == rhs.kind
        && lhs.trackId == rhs.trackId
        && lhs.sendTargetTrackId == rhs.sendTargetTrackId
        && lhs.deviceSlotId == rhs.deviceSlotId
        && lhs.pluginParameterId == rhs.pluginParameterId;
}

bool operator!= (const AutomationTarget& lhs, const AutomationTarget& rhs) noexcept
{
    return ! (lhs == rhs);
}

AutomationPoint::AutomationPoint (time::TickPosition positionValue,
                                  double value,
                                  AutomationInterpolation interpolation)
    : position (positionValue),
      normalizedValue (normalized (value)),
      interpolationToNext (interpolation)
{
    if (position.ticks() < 0)
        throw std::invalid_argument ("Automation point position must not be negative");
}

const std::vector<AutomationPoint>& AutomationCurve::points() const noexcept
{
    return points_;
}

bool AutomationCurve::empty() const noexcept
{
    return points_.empty();
}

void AutomationCurve::addPoint (AutomationPoint point)
{
    const auto insertionPoint = std::lower_bound (points_.begin(), points_.end(), point.position, [] (const auto& existing, auto position) {
        return existing.position < position;
    });

    if (insertionPoint != points_.end() && insertionPoint->position == point.position)
        throw std::invalid_argument ("Automation curve already has a point at this position");

    points_.insert (insertionPoint, point);
}

AutomationPoint AutomationCurve::removePointAt (time::TickPosition position)
{
    const auto match = std::lower_bound (points_.begin(), points_.end(), position, [] (const auto& point, auto value) {
        return point.position < value;
    });

    if (match == points_.end() || match->position != position)
        throw std::invalid_argument ("Automation curve does not contain a point at this position");

    auto removed = *match;
    points_.erase (match);
    return removed;
}

void AutomationCurve::clear() noexcept
{
    points_.clear();
}

double AutomationCurve::valueAt (time::TickPosition position, double defaultValue) const
{
    normalized (defaultValue);

    if (points_.empty() || position < points_.front().position)
        return defaultValue;

    if (position >= points_.back().position)
        return points_.back().normalizedValue;

    const auto next = std::upper_bound (points_.begin(), points_.end(), position, [] (auto value, const auto& point) {
        return value < point.position;
    });

    if (next == points_.begin())
        return defaultValue;

    const auto previous = next - 1;
    if (previous->interpolationToNext == AutomationInterpolation::hold)
        return previous->normalizedValue;

    const auto span = static_cast<double> ((next->position - previous->position).ticks());
    if (span <= 0.0)
        return previous->normalizedValue;

    const auto offset = static_cast<double> ((position - previous->position).ticks());
    const auto alpha = offset / span;
    return previous->normalizedValue + ((next->normalizedValue - previous->normalizedValue) * alpha);
}

AutomationLane::AutomationLane (AutomationTarget target, AutomationCurve curve)
    : target_ (std::move (target)),
      curve_ (std::move (curve))
{
    if (! target_.isValid())
        throw std::invalid_argument ("Automation lane requires a valid target");
}

const AutomationTarget& AutomationLane::target() const noexcept
{
    return target_;
}

const AutomationCurve& AutomationLane::curve() const noexcept
{
    return curve_;
}

AutomationCurve& AutomationLane::curve() noexcept
{
    return curve_;
}

bool AutomationLane::visible() const noexcept
{
    return visible_;
}

void AutomationLane::setVisible (bool visible) noexcept
{
    visible_ = visible;
}
}
