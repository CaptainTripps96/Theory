#include "core/sequencing/AutomationPlayback.h"

#include "core/diagnostics/PerformanceTrace.h"
#include "core/sequencing/MixerMath.h"
#include "core/sequencing/MixerStrip.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace tsq::core::sequencing
{
namespace
{
using TrackLookup = std::unordered_map<std::string, const Track*>;

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

TrackLookup buildTrackLookup (const Project& project)
{
    TrackLookup tracks;
    tracks.reserve (project.tracks().size());

    for (const auto& track : project.tracks())
        tracks.emplace (track.id(), &track);

    return tracks;
}

const Track* targetTrack (const TrackLookup& tracks, const AutomationTarget& target)
{
    const auto match = tracks.find (target.trackId);
    return match == tracks.end() ? nullptr : match->second;
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

bool automationTargetIsAvailable (const TrackLookup& tracks, const AutomationTarget& target)
{
    const auto* track = targetTrack (tracks, target);
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
            const auto returnTrack = tracks.find (target.sendTargetTrackId);
            return returnTrack != tracks.end()
                && returnTrack->second != nullptr
                && returnTrack->second->type() == TrackType::returnTrack;
        }

        case AutomationTargetKind::deviceBypass:
            return deviceSlotForTarget (*track, target) != nullptr;

        case AutomationTargetKind::pluginParameter:
            return deviceSlotForTarget (*track, target) != nullptr;
    }

    return false;
}

double defaultAutomationValueForTarget (const Track& track, const AutomationTarget& target)
{
    switch (target.kind)
    {
        case AutomationTargetKind::trackVolume:
            return automationValueFromVolumeDb (track.mixerStrip().volumeDb());

        case AutomationTargetKind::trackPan:
            return automationValueFromPan (track.mixerStrip().pan());

        case AutomationTargetKind::trackMute:
            return track.mixerStrip().muted() ? 1.0 : 0.0;

        case AutomationTargetKind::sendLevel:
            return normalized (sendLevelForTarget (track, target.sendTargetTrackId));

        case AutomationTargetKind::deviceBypass:
        {
            const auto* slot = deviceSlotForTarget (track, target);
            return slot != nullptr && slot->bypassed() ? 1.0 : 0.0;
        }

        case AutomationTargetKind::pluginParameter:
            return 0.0;
    }

    return 0.0;
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
    core::diagnostics::ScopedPerformanceTimer timer { "AutomationPlayback::automationPlaybackSnapshotAt" };

    AutomationPlaybackSnapshot snapshot;
    snapshot.position = position;
    auto laneCount = 0;
    const auto trackLookup = buildTrackLookup (project);

    for (const auto& track : project.tracks())
    {
        for (const auto& lane : track.automationLanes())
        {
            ++laneCount;
            const auto& target = lane.target();
            if (! automationTargetIsAvailable (trackLookup, target))
                continue;

            const auto* targetProjectTrack = targetTrack (trackLookup, target);
            if (targetProjectTrack == nullptr)
                continue;

            snapshot.values.push_back (AutomationPlaybackValue {
                target,
                lane.curve().valueAt (position, defaultAutomationValueForTarget (*targetProjectTrack, target))
            });
        }
    }

    core::diagnostics::writePerformanceTrace (
        "AutomationPlayback::automationPlaybackSnapshotAt summary lanes=" + std::to_string (laneCount)
            + " values=" + std::to_string (snapshot.values.size()),
        0);

    return snapshot;
}
}
