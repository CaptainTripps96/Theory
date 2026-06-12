#include "core/sequencing/AccidentalVisibility.h"
#include "core/sequencing/MusicalStructure.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
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

Region localBeats (int startBeat, int onePastEndBeat)
{
    return Region { beat (startBeat), beat (onePastEndBeat) };
}

Region projectBars (int firstBar, int onePastLastBar)
{
    return Region { beat ((firstBar - 1) * 4), beat ((onePastLastBar - 1) * 4) };
}

MidiClip makeClip (std::string id, int projectStartBeat = 0, int lengthBeats = 4)
{
    MidiClip clip { std::move (id), "Clip", beat (projectStartBeat), beats (lengthBeats) };
    const MusicalStructure structure;
    const HarmonicContextResolver resolver { structure };
    clip.snapshotHarmonicContext (resolver);
    return clip;
}

std::vector<std::string> laneNames (const std::vector<PitchLaneVisibility>& lanes)
{
    std::vector<std::string> result;

    for (const auto& lane : lanes)
        result.push_back (lane.spelling.toString());

    return result;
}

const PitchLaneVisibility* findLane (const std::vector<PitchLaneVisibility>& lanes, PitchClass pitchClass)
{
    for (const auto& lane : lanes)
    {
        if (lane.pitchClass == pitchClass)
            return &lane;
    }

    return nullptr;
}
}

TEST_CASE ("C Major clip shows native scale lanes by default")
{
    const ScaleLibrary scales;
    const auto clip = makeClip ("clip-1");

    CHECK (laneNames (visibleLanesForClip (clip, localBeats (0, 4), scales, false)) == std::vector<std::string> { "C", "D", "E", "F", "G", "A", "B" });
}

TEST_CASE ("Native scale lanes preserve context-aware key spellings")
{
    const ScaleLibrary scales;

    MusicalStructure fMajorStructure;
    fMajorStructure.addKeyCenterRegion (KeyCenterRegion { projectBars (1, 5), PitchClass::f() });
    MidiClip fMajorClip { "f-major", "F Major", beat (0), beats (4) };
    fMajorClip.snapshotHarmonicContext (HarmonicContextResolver { fMajorStructure });

    const auto fMajorLanes = visibleLanesForClip (fMajorClip, localBeats (0, 4), scales, false);
    const auto* bFlat = findLane (fMajorLanes, PitchClass::aSharp());
    REQUIRE (bFlat != nullptr);
    CHECK (bFlat->spelling.toString() == "Bb");

    MusicalStructure eMajorStructure;
    eMajorStructure.addKeyCenterRegion (KeyCenterRegion { projectBars (1, 5), PitchClass::e() });
    MidiClip eMajorClip { "e-major", "E Major", beat (0), beats (4) };
    eMajorClip.snapshotHarmonicContext (HarmonicContextResolver { eMajorStructure });

    const auto eMajorLanes = visibleLanesForClip (eMajorClip, localBeats (0, 4), scales, false);
    REQUIRE (findLane (eMajorLanes, PitchClass::fSharp()) != nullptr);
    REQUIRE (findLane (eMajorLanes, PitchClass::gSharp()) != nullptr);
    REQUIRE (findLane (eMajorLanes, PitchClass::cSharp()) != nullptr);
    REQUIRE (findLane (eMajorLanes, PitchClass::dSharp()) != nullptr);
    CHECK (findLane (eMajorLanes, PitchClass::fSharp())->spelling.toString() == "F#");
    CHECK (findLane (eMajorLanes, PitchClass::gSharp())->spelling.toString() == "G#");
    CHECK (findLane (eMajorLanes, PitchClass::cSharp())->spelling.toString() == "C#");
    CHECK (findLane (eMajorLanes, PitchClass::dSharp())->spelling.toString() == "D#");
}

TEST_CASE ("Used accidental lane is visible per clip")
{
    const ScaleLibrary scales;
    auto clip = makeClip ("clip-with-fsharp");
    clip.addNote (MidiNote { "note-1", MidiPitch::fromValue (66), beat (1), beats (1), 100, NoteName::fSharp() });

    const auto lanes = visibleLanesForClip (clip, localBeats (0, 4), scales, false);
    const auto* fSharp = findLane (lanes, PitchClass::fSharp());

    REQUIRE (fSharp != nullptr);
    CHECK (fSharp->spelling.toString() == "F#");
    CHECK (fSharp->status == PitchLaneStatus::usedAccidental);
    CHECK_FALSE (fSharp->greyed);
}

TEST_CASE ("A different clip without an accidental note does not show that accidental")
{
    const ScaleLibrary scales;
    const auto clip = makeClip ("clip-without-fsharp");

    const auto lanes = visibleLanesForClip (clip, localBeats (0, 4), scales, false);

    CHECK (findLane (lanes, PitchClass::fSharp()) == nullptr);
}

TEST_CASE ("Removing all notes from an accidental pitch hides that lane")
{
    const ScaleLibrary scales;
    auto clip = makeClip ("clip-with-removed-fsharp");
    clip.addNote (MidiNote { "note-1", MidiPitch::fromValue (66), beat (1), beats (1), 100, NoteName::fSharp() });
    REQUIRE (findLane (visibleLanesForClip (clip, localBeats (0, 4), scales, false), PitchClass::fSharp()) != nullptr);

    clip.removeNoteById ("note-1");

    CHECK (findLane (visibleLanesForClip (clip, localBeats (0, 4), scales, false), PitchClass::fSharp()) == nullptr);
}

TEST_CASE ("Chromatic reveal shows all pitch classes and greys unused non-scale lanes")
{
    const ScaleLibrary scales;
    const auto clip = makeClip ("clip-1");

    const auto lanes = visibleLanesForClip (clip, localBeats (0, 4), scales, true);

    REQUIRE (lanes.size() == 12);

    const auto* cSharp = findLane (lanes, PitchClass::cSharp());
    REQUIRE (cSharp != nullptr);
    CHECK (cSharp->status == PitchLaneStatus::chromaticReveal);
    CHECK (cSharp->editable);
    CHECK (cSharp->greyed);

    const auto* c = findLane (lanes, PitchClass::c());
    REQUIRE (c != nullptr);
    CHECK (c->status == PitchLaneStatus::nativeScale);
    CHECK_FALSE (c->greyed);
}

TEST_CASE ("Chromatic reveal preserves used accidental status")
{
    const ScaleLibrary scales;
    auto clip = makeClip ("clip-with-fsharp");
    clip.addNote (MidiNote { "note-1", MidiPitch::fromValue (66), beat (1), beats (1), 100, NoteName::fSharp() });

    const auto lanes = visibleLanesForClip (clip, localBeats (0, 4), scales, true);
    const auto* fSharp = findLane (lanes, PitchClass::fSharp());

    REQUIRE (fSharp != nullptr);
    CHECK (fSharp->status == PitchLaneStatus::usedAccidental);
    CHECK_FALSE (fSharp->greyed);
}

TEST_CASE ("Clip spanning C Major then C Mixolydian resolves lane visibility dynamically")
{
    const ScaleLibrary scales;
    MusicalStructure structure;
    structure.addScaleModeRegion (ScaleModeRegion { projectBars (5, 9), "Mixolydian" });

    const HarmonicContextResolver resolver { structure };
    MidiClip clip { "clip-1", "Spanning Clip", beat (0), beats (8 * 4) };
    clip.snapshotHarmonicContext (resolver);

    const auto majorLanes = visibleLanesForClip (clip, localBeats (0, 16), scales, false);
    CHECK (findLane (majorLanes, PitchClass::b()) != nullptr);
    CHECK (findLane (majorLanes, PitchClass::aSharp()) == nullptr);

    const auto mixolydianLanes = visibleLanesForClip (clip, localBeats (16, 32), scales, false);
    const auto* bFlat = findLane (mixolydianLanes, PitchClass::aSharp());

    REQUIRE (bFlat != nullptr);
    CHECK (bFlat->spelling.toString() == "Bb");
    CHECK (findLane (mixolydianLanes, PitchClass::b()) == nullptr);
}
