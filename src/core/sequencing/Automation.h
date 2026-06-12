#pragma once

#include "core/sequencing/DeviceChain.h"
#include "core/time/Tick.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::sequencing
{
enum class AutomationTargetKind
{
    trackVolume,
    trackPan,
    trackMute,
    sendLevel,
    deviceBypass,
    pluginParameter
};

struct AutomationTarget
{
    AutomationTargetKind kind = AutomationTargetKind::trackVolume;
    std::string trackId;
    std::string sendTargetTrackId;
    DeviceSlotId deviceSlotId;
    std::string pluginParameterId;

    static AutomationTarget trackVolume (std::string trackId);
    static AutomationTarget trackPan (std::string trackId);
    static AutomationTarget trackMute (std::string trackId);
    static AutomationTarget sendLevel (std::string trackId, std::string returnTrackId);
    static AutomationTarget deviceBypass (std::string trackId, DeviceSlotId deviceSlotId);
    static AutomationTarget pluginParameter (std::string trackId, DeviceSlotId deviceSlotId, std::string parameterId);

    std::string stableId() const;
    bool isValid() const noexcept;
};

bool operator== (const AutomationTarget& lhs, const AutomationTarget& rhs) noexcept;
bool operator!= (const AutomationTarget& lhs, const AutomationTarget& rhs) noexcept;

enum class AutomationInterpolation
{
    hold,
    linear
};

struct AutomationPoint
{
    time::TickPosition position {};
    double normalizedValue = 0.0;
    AutomationInterpolation interpolationToNext = AutomationInterpolation::linear;

    AutomationPoint() = default;
    AutomationPoint (time::TickPosition position,
                     double normalizedValue,
                     AutomationInterpolation interpolationToNext = AutomationInterpolation::linear);
};

class AutomationCurve
{
public:
    const std::vector<AutomationPoint>& points() const noexcept;
    bool empty() const noexcept;
    void addPoint (AutomationPoint point);
    AutomationPoint removePointAt (time::TickPosition position);
    void clear() noexcept;
    double valueAt (time::TickPosition position, double defaultValue) const;

private:
    std::vector<AutomationPoint> points_;
};

class AutomationLane
{
public:
    AutomationLane (AutomationTarget target, AutomationCurve curve = {});

    const AutomationTarget& target() const noexcept;
    const AutomationCurve& curve() const noexcept;
    AutomationCurve& curve() noexcept;
    bool visible() const noexcept;
    void setVisible (bool visible) noexcept;

private:
    AutomationTarget target_;
    AutomationCurve curve_;
    bool visible_ = true;
};
}
