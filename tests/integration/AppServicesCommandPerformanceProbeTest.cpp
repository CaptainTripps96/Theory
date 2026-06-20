#include "app/AppServices.h"
#include "core/commands/AddChordRegionCommand.h"
#include "core/commands/AddClipCommand.h"
#include "core/commands/AddKeyCenterRegionCommand.h"
#include "core/commands/AddNoteCommand.h"
#include "core/commands/AddScaleModeRegionCommand.h"
#include "core/commands/CommandStack.h"
#include "core/commands/MixerCommands.h"
#include "core/commands/MoveClipCommand.h"
#include "core/commands/MoveNoteCommand.h"
#include "core/commands/ProjectCommandContext.h"
#include "core/commands/ResizeClipCommand.h"
#include "core/commands/ResizeNoteCommand.h"
#include "core/commands/SetProjectRhythmSettingsCommand.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/music_theory/ChordQuality.h"
#include "core/music_theory/MidiPitch.h"
#include "core/music_theory/PitchClass.h"
#include "core/sequencing/Automation.h"
#include "core/sequencing/ChordRegion.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/KeyCenterRegion.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/MidiNote.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Region.h"
#include "core/sequencing/Routing.h"
#include "core/sequencing/ScaleModeRegion.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "core/time/ProjectRhythmSettings.h"
#include "core/time/Tick.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace tsq;

core::time::TickPosition beat (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote);
}

core::time::TickDuration beats (std::int64_t value)
{
    return core::time::TickDuration::fromTicks (value * core::time::ticksPerQuarterNote);
}

core::sequencing::Region beatRegion (std::int64_t startBeat, std::int64_t endBeat)
{
    return core::sequencing::Region { beat (startBeat), beat (endBeat) };
}

core::sequencing::MidiNote note (std::string id, int pitch, std::int64_t startBeat)
{
    return core::sequencing::MidiNote {
        std::move (id),
        core::music_theory::MidiPitch::fromValue (pitch),
        beat (startBeat),
        beats (1),
        96
    };
}

core::sequencing::PluginReference pluginReference (std::string id)
{
    core::sequencing::PluginReference plugin;
    plugin.pluginName = "Probe " + id;
    plugin.manufacturer = "Theory";
    plugin.format = "Probe";
    plugin.fileOrIdentifier = id;
    plugin.uniqueIdentifier = id + "-uid";
    plugin.numInputChannels = 2;
    plugin.numOutputChannels = 2;
    return plugin;
}

core::sequencing::DeviceSlot deviceSlot (std::string id)
{
    return core::sequencing::DeviceSlot {
        core::sequencing::DeviceSlotId { std::move (id) },
        pluginReference ("probe-device"),
        core::sequencing::PluginKind::audioEffect
    };
}

core::sequencing::MidiClip clipWithNotes (std::string id, std::int64_t startBeat, int noteCount)
{
    core::sequencing::MidiClip clip { id, id, beat (startBeat), beats (std::max<std::int64_t> (64, noteCount + 4)) };
    for (int index = 0; index < noteCount; ++index)
        clip.addNote (note ("note-" + std::to_string (index + 1), 60 + (index % 12), index));

    return clip;
}

void resetProjectForProbe (app::AppServices& services, int noteCount)
{
    auto& project = services.project();
    while (! project.tracks().empty())
        project.removeTrackById (project.tracks().back().id());

    core::sequencing::Track midi { "track-midi", "MIDI Probe", core::sequencing::TrackType::midi };
    midi.addClip (clipWithNotes ("clip-main", 0, noteCount));
    midi.addClip (core::sequencing::MidiClip { "clip-move", "Move Me", beat (noteCount + 64), beats (8) });
    midi.addClip (core::sequencing::MidiClip { "clip-resize", "Resize Me", beat (noteCount + 112), beats (8) });
    project.addTrack (std::move (midi));

    core::sequencing::Track audio { "track-audio", "Audio Probe", core::sequencing::TrackType::audio };
    core::sequencing::DeviceChain chain;
    chain.appendSlot (deviceSlot ("slot-1"));
    audio.setDeviceChain (std::move (chain));
    project.addTrack (std::move (audio));

    services.setSelectedTrack ("track-midi");
    services.commandStack().clearHistory();
}

template <typename CommandFactory>
void executeProbe (app::AppServices& services, std::string label, CommandFactory createCommand)
{
    core::diagnostics::ScopedPerformanceTimer timer { "AppCommandPerfProbe::" + label };
    const auto result = services.commandStack().execute (createCommand());
    REQUIRE (result.succeeded());
}

void probeAppServicesCommands (app::AppServices& services, int noteCount)
{
    resetProjectForProbe (services, noteCount);

    executeProbe (services, "execute AddNote notes=" + std::to_string (noteCount), [noteCount]
    {
        return std::make_unique<core::commands::AddNoteCommand> (
            "track-midi",
            "clip-main",
            note ("note-added-" + std::to_string (noteCount), 72, noteCount + 1));
    });

    executeProbe (services, "execute MoveNote notes=" + std::to_string (noteCount), []
    {
        return std::make_unique<core::commands::MoveNoteCommand> ("track-midi", "clip-main", "note-16", beat (24));
    });

    executeProbe (services, "execute ResizeNote notes=" + std::to_string (noteCount), []
    {
        return std::make_unique<core::commands::ResizeNoteCommand> ("track-midi", "clip-main", "note-32", beats (2));
    });

    executeProbe (services, "execute AddClip clips=3 notes=" + std::to_string (noteCount), [noteCount]
    {
        return std::make_unique<core::commands::AddClipCommand> (
            "track-midi",
            core::sequencing::MidiClip { "clip-added", "Added", beat (noteCount + 144), beats (8) });
    });

    executeProbe (services, "execute MoveClip clips=4 notes=" + std::to_string (noteCount), [noteCount]
    {
        return std::make_unique<core::commands::MoveClipCommand> ("track-midi", "clip-move", beat (noteCount + 80));
    });

    executeProbe (services, "execute ResizeClip clips=4 notes=" + std::to_string (noteCount), []
    {
        return std::make_unique<core::commands::ResizeClipCommand> ("track-midi", "clip-resize", beats (12));
    });

    executeProbe (services, "execute SetTrackMixerStrip notes=" + std::to_string (noteCount), [&services]
    {
        auto strip = services.project().findTrackById ("track-midi")->mixerStrip();
        strip.setVolumeDb (-7.5);
        strip.setPan (0.25);
        return std::make_unique<core::commands::SetTrackMixerStripCommand> ("track-midi", strip);
    });

    executeProbe (services, "execute SetTrackRouting tracks=2 notes=" + std::to_string (noteCount), []
    {
        core::sequencing::TrackRouting routing;
        routing.setAudioTo (core::sequencing::RouteEndpoint::none());
        return std::make_unique<core::commands::SetTrackRoutingCommand> ("track-audio", routing);
    });

    executeProbe (services, "execute SetTrackAutomationLane points=128 notes=" + std::to_string (noteCount), []
    {
        core::sequencing::AutomationCurve curve;
        for (int index = 0; index < 128; ++index)
            curve.addPoint (core::sequencing::AutomationPoint { beat (index), (index % 17) / 16.0 });

        return std::make_unique<core::commands::SetTrackAutomationLaneCommand> (
            "track-midi",
            core::sequencing::AutomationLane {
                core::sequencing::AutomationTarget::trackVolume ("track-midi"),
                std::move (curve)
            });
    });

    executeProbe (services, "execute AddTrackDevice devices=1 notes=" + std::to_string (noteCount), []
    {
        return std::make_unique<core::commands::AddTrackDeviceCommand> (
            "track-audio",
            deviceSlot ("slot-added"),
            1);
    });

    executeProbe (services, "execute SetTrackDeviceBypass devices=2 notes=" + std::to_string (noteCount), []
    {
        return std::make_unique<core::commands::SetTrackDeviceBypassCommand> (
            "track-audio",
            core::sequencing::DeviceSlotId { "slot-1" },
            true);
    });

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AppCommandPerfProbe::undo last command notes=" + std::to_string (noteCount)
        };
        REQUIRE (services.commandStack().undo().succeeded());
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AppCommandPerfProbe::redo last command notes=" + std::to_string (noteCount)
        };
        REQUIRE (services.commandStack().redo().succeeded());
    }
}

void probeHarmonicCommandsDoNotDirtyPlayback (app::AppServices& services)
{
    resetProjectForProbe (services, 32);

    services.markPlaybackProjectDirty();
    REQUIRE (services.syncPlaybackProjectIfNeeded());
    REQUIRE_FALSE (services.playbackProjectDirty());
    CHECK (services.playbackProjectDirtyCategories() == core::commands::PlaybackSyncCategory::none);

    executeProbe (services, "execute AddKeyCenterRegion playback-skip", []
    {
        return std::make_unique<core::commands::AddKeyCenterRegionCommand> (
            core::sequencing::KeyCenterRegion { beatRegion (16, 32), core::music_theory::PitchClass::g() });
    });
    CHECK_FALSE (services.playbackProjectDirty());
    CHECK (services.playbackProjectDirtyCategories() == core::commands::PlaybackSyncCategory::none);
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    executeProbe (services, "execute AddScaleModeRegion playback-skip", []
    {
        return std::make_unique<core::commands::AddScaleModeRegionCommand> (
            core::sequencing::ScaleModeRegion { beatRegion (16, 32), "Lydian" });
    });
    CHECK_FALSE (services.playbackProjectDirty());
    CHECK (services.playbackProjectDirtyCategories() == core::commands::PlaybackSyncCategory::none);
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    executeProbe (services, "execute AddChordRegion playback-skip", []
    {
        return std::make_unique<core::commands::AddChordRegionCommand> (
            core::sequencing::ChordRegion {
                beatRegion (0, 8),
                core::music_theory::PitchClass::c(),
                core::music_theory::ChordQuality::major,
                std::vector<core::music_theory::PitchClass> {
                    core::music_theory::PitchClass::c(),
                    core::music_theory::PitchClass::e(),
                    core::music_theory::PitchClass::g() },
                "C"
            });
    });
    CHECK_FALSE (services.playbackProjectDirty());
    CHECK (services.playbackProjectDirtyCategories() == core::commands::PlaybackSyncCategory::none);
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    executeProbe (services, "execute SetProjectRhythmSettings playback-skip", []
    {
        core::time::ProjectRhythmSettings settings;
        settings.setCurrentGridDivisionId ("eighth");
        settings.setQuintupletsEnabled (true);
        return std::make_unique<core::commands::SetProjectRhythmSettingsCommand> (settings);
    });
    CHECK_FALSE (services.playbackProjectDirty());
    CHECK (services.playbackProjectDirtyCategories() == core::commands::PlaybackSyncCategory::none);
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    executeProbe (services, "execute AddNote after harmonic playback-dirty", []
    {
        return std::make_unique<core::commands::AddNoteCommand> (
            "track-midi",
            "clip-main",
            note ("note-after-harmonic", 72, 40));
    });
    CHECK (services.playbackProjectDirty());
    CHECK (core::commands::playbackSyncRequired (services.playbackProjectDirtyCategories()));

    core::diagnostics::writePerformanceTrace ("AppCommandPerfProbe::harmonic/editor commands skipped playback dirty count=4", 0);
}

void probeCommandStackDirtyNotifications()
{
    core::sequencing::Project project { "dirty-probe", "Dirty Probe" };
    core::sequencing::Track track { "track-1", "Track 1", core::sequencing::TrackType::midi };
    track.addClip (core::sequencing::MidiClip { "clip-1", "Clip 1", {}, beats (8) });
    project.addTrack (std::move (track));

    core::commands::ProjectCommandContext context { project };
    core::commands::CommandStack stack { context };
    std::vector<core::commands::PlaybackSyncCategory> dirtyCategories;
    stack.setChangeCallback ([&dirtyCategories] (core::commands::PlaybackSyncCategory category) {
        dirtyCategories.push_back (category);
    });

    {
        core::diagnostics::ScopedPerformanceTimer timer { "CommandDirtyProbe::execute undo redo" };
        REQUIRE (stack.execute (std::make_unique<core::commands::AddNoteCommand> ("track-1", "clip-1", note ("dirty-note", 60, 0))).succeeded());
        REQUIRE (stack.undo().succeeded());
        REQUIRE (stack.redo().succeeded());
    }

    REQUIRE (dirtyCategories.size() == 3);
    CHECK (dirtyCategories[0] == core::commands::PlaybackSyncCategory::noteData);
    CHECK (dirtyCategories[1] == core::commands::PlaybackSyncCategory::noteData);
    CHECK (dirtyCategories[2] == core::commands::PlaybackSyncCategory::noteData);
    core::diagnostics::writePerformanceTrace (
        "CommandDirtyProbe::dirty notifications execute+undo+redo count=" + std::to_string (dirtyCategories.size()),
        0);
}
}

TEST_CASE ("AppServices command and dirtying paths are performance probed", "[integration][commands][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;

    probeHarmonicCommandsDoNotDirtyPlayback (services);

    for (const auto noteCount : std::array<int, 3> { 32, 512, 2048 })
        probeAppServicesCommands (services, noteCount);

    probeCommandStackDirtyNotifications();
}
