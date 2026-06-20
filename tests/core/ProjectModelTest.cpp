#include "core/sequencing/Project.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace tsq::core;
using namespace tsq::core::music_theory;
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

TickDuration beats (int count)
{
    return TickDuration::fromTicks (static_cast<std::int64_t> (count) * ticksPerQuarterNote);
}

TickPosition beat (int zeroBasedBeat)
{
    return TickPosition::fromTicks (static_cast<std::int64_t> (zeroBasedBeat) * ticksPerQuarterNote);
}

MidiClip clip (std::string id, int startBeat, int lengthBeats)
{
    return MidiClip { std::move (id), "Clip", beat (startBeat), beats (lengthBeats) };
}
}

TEST_CASE ("Project model supports multiple MIDI tracks")
{
    Project project { "project-1", "Song" };

    project.addTrack (Track { "track-1", "Piano" });
    project.addTrack (Track { "track-2", "Bass" });
    project.addTrack (Track { "track-3", "Lead" });

    CHECK (project.tracks().size() == 3);
    REQUIRE (project.findTrackById ("track-2") != nullptr);
    CHECK (project.findTrackById ("track-2")->name() == "Bass");
    CHECK (project.musicalStructure().defaultScaleDefinitionName() == "Major");
}

TEST_CASE ("Tracks accept non-overlapping clips")
{
    Track track { "track-1", "Piano" };

    track.addClip (clip ("clip-1", 0, 4));
    track.addClip (clip ("clip-2", 4, 4));
    track.addClip (clip ("clip-3", 10, 2));

    REQUIRE (track.clips().size() == 3);
    CHECK (track.clips()[0].id() == "clip-1");
    CHECK (track.clips()[1].id() == "clip-2");
    CHECK (track.clips()[2].id() == "clip-3");
}

TEST_CASE ("Tracks reject overlapping clips on the same track")
{
    Track track { "track-1", "Piano" };

    track.addClip (clip ("clip-1", 0, 4));

    CHECK_THROWS_AS (track.addClip (clip ("clip-2", 3, 4)), std::invalid_argument);
    CHECK (track.clips().size() == 1);
}

TEST_CASE ("Project allows clips at the same time on different tracks")
{
    Project project { "project-1", "Song" };

    Track piano { "track-1", "Piano" };
    piano.addClip (clip ("piano-clip", 0, 4));

    Track bass { "track-2", "Bass" };
    bass.addClip (clip ("bass-clip", 0, 4));

    project.addTrack (std::move (piano));
    project.addTrack (std::move (bass));

    REQUIRE (project.tracks().size() == 2);
    CHECK (project.tracks()[0].clips()[0].startInProject() == project.tracks()[1].clips()[0].startInProject());
}

TEST_CASE ("MIDI clips own clip-local notes")
{
    MidiClip midiClip { "clip-1", "Phrase", beat (8), beats (4) };

    midiClip.addNote (MidiNote { "note-1", MidiPitch::middleC(), TickPosition::fromTicks (0), beats (1), 100, NoteName::c() });
    midiClip.addNote (MidiNote { "note-2", MidiPitch::fromValue (64), TickPosition {} + beats (1), beats (1), 96, NoteName::e() });

    REQUIRE (midiClip.notes().size() == 2);
    CHECK (midiClip.notes()[0].id() == "note-1");
    CHECK (midiClip.notes()[0].startInClip().ticks() == 0);
    CHECK (midiClip.notes()[1].pitch() == MidiPitch::fromValue (64));
    REQUIRE (midiClip.notes()[1].spelling().has_value());
    CHECK (midiClip.notes()[1].spelling()->toString() == "E");
}

TEST_CASE ("MIDI clips can bulk-add notes while preserving sorted order")
{
    MidiClip midiClip { "clip-1", "Phrase", beat (0), beats (4) };
    midiClip.addNote (MidiNote { "existing", MidiPitch::fromValue (67), beat (1), beats (1), 100, NoteName::g() });

    std::vector<MidiNote> notes;
    notes.push_back (MidiNote { "late", MidiPitch::fromValue (72), beat (3), beats (1), 100, NoteName::c() });
    notes.push_back (MidiNote { "early", MidiPitch::middleC(), beat (0), beats (1), 100, NoteName::c() });
    notes.push_back (MidiNote { "same-start", MidiPitch::fromValue (64), beat (1), beats (1), 100, NoteName::e() });

    midiClip.addNotes (std::move (notes));

    REQUIRE (midiClip.notes().size() == 4);
    CHECK (midiClip.notes()[0].id() == "early");
    CHECK (midiClip.notes()[1].id() == "existing");
    CHECK (midiClip.notes()[2].id() == "same-start");
    CHECK (midiClip.notes()[3].id() == "late");
}

TEST_CASE ("MIDI bulk-add rejects duplicate IDs without partial mutation")
{
    MidiClip midiClip { "clip-1", "Phrase", beat (0), beats (4) };
    midiClip.addNote (MidiNote { "existing", MidiPitch::middleC(), beat (0), beats (1), 100, NoteName::c() });

    std::vector<MidiNote> notes;
    notes.push_back (MidiNote { "new", MidiPitch::fromValue (64), beat (1), beats (1), 100, NoteName::e() });
    notes.push_back (MidiNote { "existing", MidiPitch::fromValue (67), beat (2), beats (1), 100, NoteName::g() });

    CHECK_THROWS_AS (midiClip.addNotes (std::move (notes)), std::invalid_argument);
    REQUIRE (midiClip.notes().size() == 1);
    CHECK (midiClip.notes()[0].id() == "existing");
}

TEST_CASE ("MIDI bulk-add rejects out-of-range notes without partial mutation")
{
    MidiClip midiClip { "clip-1", "Phrase", beat (0), beats (2) };

    std::vector<MidiNote> notes;
    notes.push_back (MidiNote { "inside", MidiPitch::middleC(), beat (0), beats (1), 100, NoteName::c() });
    notes.push_back (MidiNote { "outside", MidiPitch::fromValue (64), beat (1), beats (2), 100, NoteName::e() });

    CHECK_THROWS_AS (midiClip.addNotes (std::move (notes)), std::invalid_argument);
    CHECK (midiClip.notes().empty());
}

TEST_CASE ("MIDI notes reject invalid durations")
{
    CHECK_THROWS_AS (
        MidiNote ("note-1", MidiPitch::middleC(), TickPosition::fromTicks (0), TickDuration::fromTicks (0), 100),
        std::invalid_argument);

    CHECK_THROWS_AS (
        MidiNote ("note-2", MidiPitch::middleC(), TickPosition::fromTicks (0), TickDuration::fromTicks (-1), 100),
        std::invalid_argument);
}

TEST_CASE ("Clip local and project tick conversion remains separate")
{
    const MidiClip midiClip { "clip-1", "Phrase", beat (8), beats (4) };

    CHECK (midiClip.localToProject (TickPosition {} + beats (2)) == beat (10));
    CHECK (midiClip.projectToLocal (beat (9)) == TickPosition {} + beats (1));
}

TEST_CASE ("Clip loop repetitions can be calculated")
{
    const MidiClip midiClip {
        "clip-1",
        "Looped Phrase",
        beat (0),
        beats (6),
        ClipLoop::enabled (beats (2))
    };

    const auto repetitions = midiClip.loopRepetitions();

    REQUIRE (repetitions.size() == 3);
    CHECK (repetitions[0].start().ticks() == 0);
    CHECK (repetitions[0].end().ticks() == beats (2).ticks());
    CHECK (repetitions[1].start().ticks() == beats (2).ticks());
    CHECK (repetitions[1].end().ticks() == beats (4).ticks());
    CHECK (repetitions[2].start().ticks() == beats (4).ticks());
    CHECK (repetitions[2].end().ticks() == beats (6).ticks());
}

TEST_CASE ("Looped MIDI clips keep source length separate from visible arrangement length")
{
    MidiClip midiClip {
        "clip-1",
        "Looped Phrase",
        beat (0),
        beats (8),
        ClipLoop::enabled (beats (2))
    };

    CHECK (midiClip.length() == beats (8));
    CHECK (midiClip.sourceLength() == beats (2));

    midiClip.addNote (MidiNote { "note-1", MidiPitch::middleC(), beat (1), beats (1), 100, NoteName::c() });
    CHECK_THROWS_AS (
        midiClip.addNote (MidiNote { "note-2", MidiPitch::middleC(), beat (2), beats (1), 100, NoteName::c() }),
        std::invalid_argument);
}

TEST_CASE ("Looped MIDI clips can be shortened without changing source note data")
{
    MidiClip midiClip {
        "clip-1",
        "Looped Phrase",
        beat (0),
        beats (8),
        ClipLoop::enabled (beats (4))
    };

    midiClip.addNote (MidiNote { "note-1", MidiPitch::middleC(), beat (3), beats (1), 100, NoteName::c() });

    const auto shortened = midiClip.withLength (beats (2));

    CHECK (shortened.length() == beats (2));
    CHECK (shortened.sourceLength() == beats (4));
    REQUIRE (shortened.notes().size() == 1);
    CHECK (shortened.notes()[0].startInClip() == beat (3));
}
