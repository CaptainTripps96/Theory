#pragma once

#include "core/music_theory/MidiPitch.h"
#include "core/sequencing/AudioClip.h"
#include "core/sequencing/Automation.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/MidiNote.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Routing.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "core/time/Tick.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tsq::tests::performance
{
namespace detail
{
inline core::time::TickPosition beat (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote);
}

inline core::time::TickDuration beats (std::int64_t value)
{
    return core::time::TickDuration::fromTicks (value * core::time::ticksPerQuarterNote);
}

inline core::time::TickPosition sixteenth (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote / 4);
}

inline core::time::TickDuration sixteenthDuration()
{
    return core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 4);
}
}

struct SyntheticProjectSpec
{
    std::string idPrefix = "synthetic";
    int trackCount = 16;
    int midiNoteCount = 100;
    int automationPointCount = 10;
    int audioClipsPerAudioTrack = 2;
    int returnTrackCount = 2;
    int deviceSlotsPerTrack = 2;
    bool includeAudioClips = true;
    bool includeDeviceChains = true;
    bool includeReturnSends = true;
};

struct SyntheticProjectSummary
{
    int tracks = 0;
    int midiTracks = 0;
    int audioTracks = 0;
    int returnTracks = 0;
    int midiClips = 0;
    int midiNotes = 0;
    int audioClips = 0;
    int automationLanes = 0;
    int automationPoints = 0;
    int deviceSlots = 0;

    std::string label() const
    {
        return "tracks=" + std::to_string (tracks)
            + " midiTracks=" + std::to_string (midiTracks)
            + " audioTracks=" + std::to_string (audioTracks)
            + " returnTracks=" + std::to_string (returnTracks)
            + " midiClips=" + std::to_string (midiClips)
            + " midiNotes=" + std::to_string (midiNotes)
            + " audioClips=" + std::to_string (audioClips)
            + " automationLanes=" + std::to_string (automationLanes)
            + " automationPoints=" + std::to_string (automationPoints)
            + " deviceSlots=" + std::to_string (deviceSlots);
    }
};

inline core::sequencing::PluginReference pluginReference (std::string id,
                                                          core::sequencing::PluginKind kind)
{
    core::sequencing::PluginReference plugin;
    plugin.pluginName = "Synthetic " + id;
    plugin.manufacturer = "TheorySequencer";
    plugin.format = "Synthetic";
    plugin.fileOrIdentifier = "/tmp/TheorySequencerSyntheticProjectFixtures/" + id + ".vst3";
    plugin.uniqueIdentifier = "synthetic:" + id;
    plugin.uniqueId = static_cast<int> (std::hash<std::string> {} (id) & 0x7fffffff);
    plugin.numInputChannels = kind == core::sequencing::PluginKind::instrument ? 0 : 2;
    plugin.numOutputChannels = 2;
    return plugin;
}

inline core::sequencing::DeviceSlot deviceSlot (std::string id,
                                                core::sequencing::PluginKind kind)
{
    return core::sequencing::DeviceSlot {
        core::sequencing::DeviceSlotId { id },
        pluginReference (std::move (id), kind),
        kind
    };
}

inline core::sequencing::DeviceChain deviceChainForTrack (const std::string& trackId,
                                                          core::sequencing::TrackType trackType,
                                                          int slotCount)
{
    core::sequencing::DeviceChain chain;
    if (slotCount <= 0)
        return chain;

    if (trackType == core::sequencing::TrackType::midi)
    {
        chain.appendSlot (deviceSlot (trackId + "-instrument", core::sequencing::PluginKind::instrument));
        for (auto index = 1; index < slotCount; ++index)
            chain.appendSlot (deviceSlot (trackId + "-fx-" + std::to_string (index), core::sequencing::PluginKind::audioEffect));
        return chain;
    }

    if (core::sequencing::trackTypeCanHostAudioEffects (trackType))
    {
        for (auto index = 0; index < slotCount; ++index)
            chain.appendSlot (deviceSlot (trackId + "-fx-" + std::to_string (index + 1), core::sequencing::PluginKind::audioEffect));
    }

    return chain;
}

inline core::sequencing::MidiNote midiNote (std::string id,
                                            int noteIndex,
                                            std::int64_t sixteenthStart)
{
    return core::sequencing::MidiNote {
        std::move (id),
        core::music_theory::MidiPitch::fromValue (48 + ((noteIndex * 5) % 36)),
        detail::sixteenth (sixteenthStart),
        detail::sixteenthDuration(),
        96
    };
}

inline core::sequencing::MidiClip midiClip (std::string id,
                                            int trackIndex,
                                            int noteCount,
                                            std::int64_t startBeat)
{
    const auto clipLengthBeats = std::max<std::int64_t> (32, (noteCount / 4) + 8);
    core::sequencing::MidiClip clip {
        id,
        id,
        detail::beat (startBeat),
        detail::beats (clipLengthBeats)
    };

    if (noteCount > 0)
        clip.reserveNotes (static_cast<std::size_t> (noteCount) + std::max<std::size_t> (8, static_cast<std::size_t> (noteCount) / 8));

    const auto usableSixteenthSlots = std::max<std::int64_t> (1, clipLengthBeats * 4 - 4);
    std::vector<core::sequencing::MidiNote> notes;
    notes.reserve (static_cast<std::size_t> (std::max (0, noteCount)));
    for (auto noteIndex = 0; noteIndex < noteCount; ++noteIndex)
    {
        notes.push_back (midiNote ("note-" + std::to_string (trackIndex) + "-" + std::to_string (noteIndex),
                                   noteIndex,
                                   noteIndex % usableSixteenthSlots));
    }
    clip.addNotes (std::move (notes));

    return clip;
}

inline core::sequencing::AutomationCurve automationCurve (int pointCount, int trackIndex)
{
    core::sequencing::AutomationCurve curve;
    for (auto pointIndex = 0; pointIndex < pointCount; ++pointIndex)
    {
        curve.addPoint (core::sequencing::AutomationPoint {
            detail::sixteenth (pointIndex * 2),
            static_cast<double> ((pointIndex + trackIndex) % 101) / 100.0
        });
    }

    return curve;
}

inline core::sequencing::AudioClip audioClip (const std::string& id,
                                              int clipIndex,
                                              std::int64_t startBeat)
{
    core::sequencing::AudioSourceReference source {
        "source-" + id,
        "/tmp/TheorySequencerSyntheticProjectFixtures/" + id + ".wav",
        id + ".wav",
        false
    };

    auto clip = core::sequencing::AudioClip {
        id,
        id,
        std::move (source),
        detail::beat (startBeat),
        detail::beats (8)
    };
    clip.setGainDb (-static_cast<double> (clipIndex % 12));
    clip.setLoopEnabled (clipIndex % 2 == 0);
    return clip;
}

inline int distributedCount (int total, int index, int bucketCount)
{
    if (bucketCount <= 0)
        return 0;

    const auto base = total / bucketCount;
    const auto remainder = total % bucketCount;
    return base + (index < remainder ? 1 : 0);
}

inline core::sequencing::Project makeSyntheticProject (SyntheticProjectSpec spec)
{
    spec.trackCount = std::max (1, spec.trackCount);
    spec.midiNoteCount = std::max (0, spec.midiNoteCount);
    spec.automationPointCount = std::max (0, spec.automationPointCount);
    spec.audioClipsPerAudioTrack = std::max (0, spec.audioClipsPerAudioTrack);
    spec.deviceSlotsPerTrack = std::max (0, spec.deviceSlotsPerTrack);

    const auto returnTrackCount = std::clamp (spec.returnTrackCount, 0, std::max (0, spec.trackCount - 1));
    const auto remainingTracks = spec.trackCount - returnTrackCount;
    const auto audioTrackCount = spec.includeAudioClips ? std::max (1, remainingTracks / 4) : 0;
    const auto midiTrackCount = std::max (0, remainingTracks - audioTrackCount);
    const auto automatableTrackCount = std::max (1, midiTrackCount + audioTrackCount);

    core::sequencing::Project project {
        spec.idPrefix + "-" + std::to_string (spec.trackCount)
            + "-" + std::to_string (spec.midiNoteCount)
            + "-" + std::to_string (spec.automationPointCount),
        "Synthetic " + std::to_string (spec.trackCount) + " Track Project"
    };

    std::vector<std::string> returnTrackIds;
    returnTrackIds.reserve (static_cast<std::size_t> (returnTrackCount));
    for (auto returnIndex = 0; returnIndex < returnTrackCount; ++returnIndex)
    {
        const auto trackId = "return-" + std::to_string (returnIndex + 1);
        returnTrackIds.push_back (trackId);

        core::sequencing::Track track {
            trackId,
            "Return " + std::to_string (returnIndex + 1),
            core::sequencing::TrackType::returnTrack
        };

        if (spec.includeDeviceChains)
            track.setDeviceChain (deviceChainForTrack (trackId, track.type(), spec.deviceSlotsPerTrack));

        project.addTrack (std::move (track));
    }

    for (auto midiIndex = 0; midiIndex < midiTrackCount; ++midiIndex)
    {
        const auto trackId = "midi-" + std::to_string (midiIndex + 1);
        core::sequencing::Track track {
            trackId,
            "MIDI " + std::to_string (midiIndex + 1),
            core::sequencing::TrackType::midi
        };

        if (spec.includeDeviceChains)
            track.setDeviceChain (deviceChainForTrack (trackId, track.type(), spec.deviceSlotsPerTrack));

        auto routing = track.routing();
        if (spec.includeReturnSends && ! returnTrackIds.empty())
            routing.addOrReplaceSend (core::sequencing::ReturnSend { returnTrackIds[static_cast<std::size_t> (midiIndex) % returnTrackIds.size()],
                                                                      0.25 + (0.5 * static_cast<double> (midiIndex % 3) / 2.0) });
        track.setRouting (std::move (routing));

        track.addClip (midiClip ("clip-midi-" + std::to_string (midiIndex + 1),
                                 midiIndex,
                                 distributedCount (spec.midiNoteCount, midiIndex, midiTrackCount),
                                 0));

        const auto automationPoints = distributedCount (spec.automationPointCount, midiIndex, automatableTrackCount);
        if (automationPoints > 0)
        {
            track.setAutomationLane (core::sequencing::AutomationLane {
                core::sequencing::AutomationTarget::trackVolume (trackId),
                automationCurve (automationPoints, midiIndex)
            });
        }

        project.addTrack (std::move (track));
    }

    for (auto audioIndex = 0; audioIndex < audioTrackCount; ++audioIndex)
    {
        const auto trackId = "audio-" + std::to_string (audioIndex + 1);
        core::sequencing::Track track {
            trackId,
            "Audio " + std::to_string (audioIndex + 1),
            core::sequencing::TrackType::audio
        };

        if (spec.includeDeviceChains)
            track.setDeviceChain (deviceChainForTrack (trackId, track.type(), spec.deviceSlotsPerTrack));

        for (auto clipIndex = 0; clipIndex < spec.audioClipsPerAudioTrack; ++clipIndex)
        {
            track.addAudioClip (audioClip ("clip-audio-" + std::to_string (audioIndex + 1)
                                           + "-" + std::to_string (clipIndex + 1),
                                           clipIndex,
                                           clipIndex * 12));
        }

        const auto automationPoints = distributedCount (spec.automationPointCount, midiTrackCount + audioIndex, automatableTrackCount);
        if (automationPoints > 0)
        {
            track.setAutomationLane (core::sequencing::AutomationLane {
                core::sequencing::AutomationTarget::trackPan (trackId),
                automationCurve (automationPoints, audioIndex)
            });
        }

        project.addTrack (std::move (track));
    }

    return project;
}

inline SyntheticProjectSummary summarize (const core::sequencing::Project& project)
{
    SyntheticProjectSummary summary;
    summary.tracks = static_cast<int> (project.tracks().size());

    for (const auto& track : project.tracks())
    {
        switch (track.type())
        {
            case core::sequencing::TrackType::midi: ++summary.midiTracks; break;
            case core::sequencing::TrackType::audio: ++summary.audioTracks; break;
            case core::sequencing::TrackType::returnTrack: ++summary.returnTracks; break;
            case core::sequencing::TrackType::master: break;
        }

        summary.midiClips += static_cast<int> (track.clips().size());
        summary.audioClips += static_cast<int> (track.audioClips().size());
        summary.automationLanes += static_cast<int> (track.automationLanes().size());
        summary.deviceSlots += static_cast<int> (track.deviceChain().size());

        for (const auto& clip : track.clips())
            summary.midiNotes += static_cast<int> (clip.notes().size());

        for (const auto& lane : track.automationLanes())
            summary.automationPoints += static_cast<int> (lane.curve().points().size());
    }

    return summary;
}
}
