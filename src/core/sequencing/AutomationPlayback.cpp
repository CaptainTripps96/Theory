#include "core/sequencing/AutomationPlayback.h"

#include "core/sequencing/MixerMath.h"
#include "core/sequencing/MixerStrip.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tsq::core::sequencing
{
namespace
{
double normalized (double value)
{
    if (std::isnan (value))
        throw std::invalid_argument ("Automation value must not be NaN");

    return std::clamp (value, 0.0, 1.0);
}

const Track* targetTrack (const Project& project, const AutomationTarget& target)
{
    return project.findTrackById (target.trackId);
}

double sendLevelForTarget (const Track& track, const std::string& returnTrackId)
{
    const auto& sends = track.routing().sends();
    const auto match = std::find_if (sends.begin(), sends.end(), [&returnTrackId] (const auto& send) {
        return send.targetReturnTrackId == returnTrackId;
    });

    return match == sends.end() ? 0.0 : match->normalizedLevel;
}

const DeviceSlot* deviceSlotForTarget (const Track& track, const AutomationTarget& target)
{
    return track.deviceChain().findSlot (target.deviceSlotId);
}

}

double automationValueFromVolumeDb (double volumeDb)
{
    if (MixerStrip::isSilenceDb (volumeDb))
        return 0.0;

    const auto clamped = MixerStrip::clampVolumeDb (volumeDb);
    const auto range = MixerStrip::maximumVolumeDb - MixerStrip::minimumFiniteVolumeDb;
    return range <= 0.0 ? 1.0 : normalized ((clamped - MixerStrip::minimumFiniteVolumeDb) / range);
}

double volumeDbFromAutomationValue (double normalizedValue)
{
    const auto value = normalized (normalizedValue);
    const auto range = MixerStrip::maximumVolumeDb - MixerStrip::minimumFiniteVolumeDb;
    return MixerStrip::clampVolumeDb (MixerStrip::minimumFiniteVolumeDb + (value * range));
}

double automationValueFromPan (double pan)
{
    return normalized ((clampPan (pan) + 1.0) * 0.5);
}

double panFromAutomationValue (double normalizedValue)
{
    return clampPan ((normalized (normalizedValue) * 2.0) - 1.0);
}

bool automationTargetIsAvailable (const Project& project, const AutomationTarget& target)
{
    const auto* track = targetTrack (project, target);
    if (track == nullptr || ! target.isValid())
        return false;

    switch (target.kind)
    {
        case AutomationTargetKind::trackVolume:
        case AutomationTargetKind::trackPan:
        case AutomationTargetKind::trackMute:
            return true;

        case AutomationTargetKind::sendLevel:
        {
            const auto* returnTrack = project.findTrackById (target.sendTargetTrackId);
            return returnTrack != nullptr && returnTrack->type() == TrackType::returnTrack;
        }

        case AutomationTargetKind::deviceBypass:
            return deviceSlotForTarget (*track, target) != nullptr;

        case AutomationTargetKind::pluginParameter:
            return deviceSlotForTarget (*track, target) != nullptr;
    }

    return false;
}

double defaultAutomationValueForTarget (const Project& project, const AutomationTarget& target)
{
    const auto* track = targetTrack (project, target);
    if (track == nullptr)
        return 0.0;

    switch (target.kind)
    {
        case AutomationTargetKind::trackVolume:
            return automationValueFromVolumeDb (track->mixerStrip().volumeDb());

        case AutomationTargetKind::trackPan:
            return automationValueFromPan (track->mixerStrip().pan());

        case AutomationTargetKind::trackMute:
            return track->mixerStrip().muted() ? 1.0 : 0.0;

        case AutomationTargetKind::sendLevel:
            return normalized (sendLevelForTarget (*track, target.sendTargetTrackId));

        case AutomationTargetKind::deviceBypass:
        {
            const auto* slot = deviceSlotForTarget (*track, target);
            return slot != nullptr && slot->bypassed() ? 1.0 : 0.0;
        }

        case AutomationTargetKind::pluginParameter:
            return 0.0;
    }

    return 0.0;
}

AutomationPlaybackSnapshot automationPlaybackSnapshotAt (const Project& project, time::TickPosition position)
{
    AutomationPlaybackSnapshot snapshot;
    snapshot.position = position;

    for (const auto& track : project.tracks())
    {
        for (const auto& lane : track.automationLanes())
        {
            const auto& target = lane.target();
            if (! automationTargetIsAvailable (project, target))
                continue;

            snapshot.values.push_back (AutomationPlaybackValue {
                target,
                lane.curve().valueAt (position, defaultAutomationValueForTarget (project, target))
            });
        }
    }

    return snapshot;
}
}
