#include "core/sequencing/ExpressionReleaseGhosts.h"

#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/devices/SimpleOscComplexSynth.h"

#include <algorithm>
#include <cmath>

namespace tsq::core::sequencing
{
time::TickDuration releaseSecondsToTicksAt (const time::TempoMap& tempoMap,
                                            time::TickPosition projectNoteEnd,
                                            double releaseSeconds)
{
    if (! std::isfinite (releaseSeconds) || releaseSeconds <= 0.0)
        return time::TickDuration {};

    const auto startSeconds = tempoMap.secondsAt (projectNoteEnd);
    const auto releaseEnd = tempoMap.tickAtSeconds (startSeconds + releaseSeconds);
    return releaseEnd - projectNoteEnd;
}

std::optional<double> firstPartyReleaseSecondsForTrack (const Track& track)
{
    for (const auto& slot : track.deviceChain().slots())
    {
        const auto& state = slot.firstPartyDevice();
        if (! state.has_value())
            continue;

        if (state->typeId == devices::simpleOscComplexTypeId())
            return devices::simpleOscComplexPatchFromState (*state).releaseSeconds;
    }

    return std::nullopt;
}

std::optional<time::TickDuration> firstPartyReleaseDurationForTrackAt (const Project& project,
                                                                       const Track& track,
                                                                       time::TickPosition projectNoteEnd)
{
    const auto seconds = firstPartyReleaseSecondsForTrack (track);
    if (! seconds.has_value())
        return std::nullopt;

    return releaseSecondsToTicksAt (project.tempoMap(), projectNoteEnd, *seconds);
}

std::vector<ReleaseGhostNote> computeReleaseGhostNotes (const Project& project,
                                                        const Track& track,
                                                        const MidiClip& clip)
{
    std::vector<ReleaseGhostNote> ghosts;
    ghosts.reserve (clip.notes().size());

    for (const auto& note : clip.notes())
    {
        const auto projectNoteEnd = clip.localToProject (note.endInClip());
        const auto releaseDuration = firstPartyReleaseDurationForTrackAt (project, track, projectNoteEnd);
        if (! releaseDuration.has_value() || releaseDuration->ticks() <= 0)
            continue;

        const auto ghostStart = note.endInClip();
        const auto ghostEnd = ghostStart + *releaseDuration;
        if (ghostEnd <= ghostStart)
            continue;

        ghosts.push_back (ReleaseGhostNote {
            note.id(),
            Region { note.startInClip(), note.endInClip() },
            Region { ghostStart, ghostEnd },
            Region { note.startInClip(), ghostEnd },
            *releaseDuration
        });
    }

    return ghosts;
}
}
