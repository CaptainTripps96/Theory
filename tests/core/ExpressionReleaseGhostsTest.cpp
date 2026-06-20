#include "core/sequencing/ExpressionReleaseGhosts.h"

#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/time/Tempo.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace
{
using namespace tsq::core;

time::TickPosition beat (int beatIndex)
{
    return time::TickPosition::fromTicks (static_cast<std::int64_t> (beatIndex) * time::ticksPerQuarterNote);
}

time::TickDuration beats (int beatCount)
{
    return time::TickDuration::fromTicks (static_cast<std::int64_t> (beatCount) * time::ticksPerQuarterNote);
}

sequencing::DeviceSlot simpleOscSlotWithRelease (double normalizedRelease)
{
    auto state = devices::defaultFirstPartyDeviceState (devices::simpleOscComplexDefinition());
    for (auto& parameter : state.parameterValues)
        if (parameter.parameterId == "amp.release")
            parameter.normalizedValue = normalizedRelease;

    return sequencing::DeviceSlot {
        sequencing::DeviceSlotId { "simple-osc-complex" },
        state,
        sequencing::PluginKind::instrument
    };
}
}

TEST_CASE ("Release seconds convert to ticks at the tempo of the note end")
{
    time::TempoMap tempoMap { time::Tempo { 120.0 } };
    CHECK (sequencing::releaseSecondsToTicksAt (tempoMap, beat (2), 1.0) == beats (2));
    CHECK (sequencing::releaseSecondsToTicksAt (tempoMap, beat (2), 0.5) == beats (1));

    time::TempoMap slowTempoMap { time::Tempo { 60.0 } };
    CHECK (sequencing::releaseSecondsToTicksAt (slowTempoMap, beat (2), 1.0) == beats (1));
}

TEST_CASE ("Release ghost notes extend phrase bounds without mutating note duration")
{
    sequencing::Project project { "project-1", "Release Ghosts" };
    sequencing::Track track { "track-1", "Lead" };
    sequencing::DeviceChain chain;
    chain.appendSlot (simpleOscSlotWithRelease (0.05));
    track.setDeviceChain (chain);

    sequencing::MidiClip clip { "clip-1", "Phrase", beat (0), beats (8) };
    clip.addNote (sequencing::MidiNote {
        "note-1",
        music_theory::MidiPitch::fromValue (60),
        beat (1),
        beats (1),
        100
    });
    track.addClip (clip);
    project.addTrack (track);

    const auto* storedTrack = project.findTrackById ("track-1");
    REQUIRE (storedTrack != nullptr);
    const auto* storedClip = storedTrack->findClipById ("clip-1");
    REQUIRE (storedClip != nullptr);

    const auto ghosts = sequencing::computeReleaseGhostNotes (project, *storedTrack, *storedClip);
    REQUIRE (ghosts.size() == 1);
    CHECK (ghosts.front().noteId == "note-1");
    CHECK (ghosts.front().noteRegion.start() == beat (1));
    CHECK (ghosts.front().noteRegion.end() == beat (2));
    CHECK (ghosts.front().ghostRegion.start() == beat (2));
    CHECK (ghosts.front().ghostRegion.end() > beat (2));
    CHECK (ghosts.front().phraseRegion.start() == beat (1));
    CHECK (ghosts.front().phraseRegion.end() == ghosts.front().ghostRegion.end());
    CHECK (storedClip->notes().front().duration() == beats (1));
}
