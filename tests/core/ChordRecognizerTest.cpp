#include "core/music_theory/ChordNameFormatter.h"
#include "core/music_theory/ChordRecognizer.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
using namespace tsq::core::music_theory;

MidiPitch pitch (int midiPitch)
{
    return MidiPitch::fromValue (midiPitch);
}

std::string chordNameFor (std::vector<MidiPitch> pitches)
{
    const ChordRecognizer recognizer;
    const ChordNameFormatter formatter;
    const auto chord = recognizer.recognize (pitches);
    REQUIRE (chord.has_value());
    return formatter.format (*chord, SpellingPreference::preferFlats);
}

ChordQuality chordQualityFor (std::vector<MidiPitch> pitches)
{
    const ChordRecognizer recognizer;
    const auto chord = recognizer.recognize (pitches);
    REQUIRE (chord.has_value());
    return chord->quality();
}
}

TEST_CASE ("Chord recognizer identifies major triads")
{
    CHECK (chordNameFor ({ pitch (60), pitch (64), pitch (67) }) == "C");
}

TEST_CASE ("Chord recognizer normalizes major triad inversions")
{
    CHECK (chordNameFor ({ pitch (64), pitch (67), pitch (72) }) == "C");
}

TEST_CASE ("Chord recognizer identifies suspended chords")
{
    CHECK (chordNameFor ({ pitch (60), pitch (65), pitch (67) }) == "Csus4");
    CHECK (chordNameFor ({ pitch (60), pitch (62), pitch (67) }) == "Csus2");
}

TEST_CASE ("Chord recognizer identifies seventh chords and inversions")
{
    CHECK (chordNameFor ({ pitch (60), pitch (64), pitch (67), pitch (71) }) == "Cmaj7");
    CHECK (chordNameFor ({ pitch (60), pitch (64), pitch (67), pitch (70) }) == "C7");
    CHECK (chordNameFor ({ pitch (59), pitch (60), pitch (64), pitch (67) }) == "Cmaj7");
}

TEST_CASE ("Chord recognizer identifies minor and altered triads")
{
    CHECK (chordNameFor ({ pitch (60), pitch (63), pitch (67) }) == "Cm");
    CHECK (chordNameFor ({ pitch (60), pitch (63), pitch (66) }) == "Cdim");
    CHECK (chordNameFor ({ pitch (60), pitch (64), pitch (68) }) == "Caug");
}

TEST_CASE ("Chord recognizer identifies remaining required seventh qualities")
{
    CHECK (chordNameFor ({ pitch (60), pitch (63), pitch (67), pitch (70) }) == "Cm7");
    CHECK (chordNameFor ({ pitch (60), pitch (63), pitch (66), pitch (70) }) == "Cm7b5");
    CHECK (chordNameFor ({ pitch (60), pitch (63), pitch (66), pitch (69) }) == "Cdim7");
}

TEST_CASE ("Chord recognizer identifies practical optional qualities")
{
    CHECK (chordQualityFor ({ pitch (60), pitch (63), pitch (67), pitch (71) }) == ChordQuality::minorMajorSeventh);
    CHECK (chordNameFor ({ pitch (60), pitch (62), pitch (64), pitch (67) }) == "Cadd9");
}

TEST_CASE ("Chord recognizer identifies power chords")
{
    CHECK (chordNameFor ({ pitch (60), pitch (67) }) == "C5");
}

TEST_CASE ("Chord recognizer reports no chord for insufficient material")
{
    const ChordRecognizer recognizer;
    CHECK_FALSE (recognizer.recognize (std::vector<MidiPitch> { pitch (60) }).has_value());
}
