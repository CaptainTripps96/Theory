#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/music_theory/MidiPitch.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/MidiNote.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "core/time/Tick.h"
#include "ui/PianoRollComponent.h"
#include "ui/TimelineComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

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

core::time::TickPosition sixteenth (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote / 4);
}

core::sequencing::MidiNote note (std::string id, int pitch, std::int64_t sixteenthStart)
{
    return core::sequencing::MidiNote {
        std::move (id),
        core::music_theory::MidiPitch::fromValue (std::clamp (pitch, 36, 96)),
        sixteenth (sixteenthStart),
        beats (1),
        96
    };
}

core::sequencing::MidiClip clipWithNotes (std::string id,
                                          std::int64_t startBeat,
                                          std::int64_t lengthBeats,
                                          int noteCount)
{
    core::sequencing::MidiClip clip { id, id, beat (startBeat), beats (lengthBeats) };
    const auto usableSlots = std::max<std::int64_t> (1, lengthBeats * 4 - 4);

    for (auto index = 0; index < noteCount; ++index)
    {
        clip.addNote (note ("note-" + std::to_string (index + 1),
                            48 + static_cast<int> ((index * 7) % 36),
                            index % usableSlots));
    }

    return clip;
}

void resetProjectForProbe (app::AppServices& services)
{
    auto& project = services.project();
    while (! project.tracks().empty())
        project.removeTrackById (project.tracks().back().id());

    for (auto trackIndex = 0; trackIndex < 12; ++trackIndex)
    {
        core::sequencing::Track track {
            "track-" + std::to_string (trackIndex + 1),
            "MIDI " + std::to_string (trackIndex + 1),
            core::sequencing::TrackType::midi
        };

        for (auto clipIndex = 0; clipIndex < 8; ++clipIndex)
        {
            const auto startBeat = static_cast<std::int64_t> (clipIndex * 8);
            const auto noteCount = trackIndex == 0 && clipIndex == 0 ? 2000 : 64;
            track.addClip (clipWithNotes ("clip-" + std::to_string (trackIndex + 1) + "-" + std::to_string (clipIndex + 1),
                                          startBeat,
                                          8,
                                          noteCount));
        }

        project.addTrack (std::move (track));
    }

    services.setSelectedTrack ("track-1");
}

void probePianoRollPlayheadMoves (app::AppServices& services)
{
    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1-1");

    {
        core::diagnostics::ScopedPerformanceTimer timer { "MidiTransportPerfProbe::piano-roll playhead moves count=4096" };
        for (auto index = 0; index < 4096; ++index)
            pianoRoll.setPlayheadTick (sixteenth (index % 32));
    }

    CHECK (pianoRoll.hasOpenClip());
}

void probeTimelinePlayheadMoves (app::AppServices& services)
{
    ui::TimelineComponent timeline { services };
    timeline.setBounds (0, 0, 1600, 1320);

    {
        core::diagnostics::ScopedPerformanceTimer timer { "MidiTransportPerfProbe::timeline playhead moves count=4096" };
        for (auto index = 0; index < 4096; ++index)
            timeline.setPlayheadTick (sixteenth (index % 256));
    }
}

void probeReturnToZero (app::AppServices& services)
{
    REQUIRE (services.setPlaybackPlayheadPosition (beat (32)));

    {
        core::diagnostics::ScopedPerformanceTimer timer { "MidiTransportPerfProbe::return to zero" };
        REQUIRE (services.returnPlaybackToStart());
    }
}
}

TEST_CASE ("MIDI recording, transport, and playhead polling paths are performance probed",
           "[integration][midi-transport][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    resetProjectForProbe (services);

    probePianoRollPlayheadMoves (services);
    probeTimelinePlayheadMoves (services);
    probeReturnToZero (services);
}
