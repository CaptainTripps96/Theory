#include <catch2/catch_test_macros.hpp>

#include "core/music_theory/EnharmonicSpelling.h"
#include "core/music_theory/MidiPitch.h"
#include "core/music_theory/ScaleDegree.h"

#include <stdexcept>

using namespace tsq::core::music_theory;

TEST_CASE("enharmonic note names share pitch class but keep spelling", "[music_theory]")
{
    const auto cSharp = NoteName::cSharp();
    const auto dFlat = NoteName::dFlat();

    REQUIRE (cSharp.pitchClass() == dFlat.pitchClass());
    REQUIRE (cSharp != dFlat);
    REQUIRE (cSharp.toString() == "C#");
    REQUIRE (dFlat.toString() == "Db");
}

TEST_CASE("midi pitch displays practical octave names", "[music_theory]")
{
    REQUIRE (MidiPitch::middleC().value() == 60);
    REQUIRE (MidiPitch::middleC().octave() == 4);
    REQUIRE (MidiPitch::middleC().nameWithOctave() == "C4");

    REQUIRE (MidiPitch { 58 }.nameWithOctave (NoteName::bFlat()) == "Bb3");
    REQUIRE (MidiPitch { 78 }.nameWithOctave() == "F#5");
    REQUIRE (MidiPitch { 61 }.nameWithOctave (SpellingPreference::preferFlats) == "Db4");
}

TEST_CASE("pitch classes transpose chromatically", "[music_theory]")
{
    REQUIRE (PitchClass::c().transposedBy (1) == PitchClass::cSharp());
    REQUIRE (PitchClass::b().transposedBy (1) == PitchClass::c());
    REQUIRE (PitchClass::c().transposedBy (-1) == PitchClass::b());
    REQUIRE (PitchClass::fSharp().transposedBy (6) == PitchClass::c());
    REQUIRE_FALSE (hasNaturalLetterName (PitchClass::fSharp()));
    REQUIRE (hasNaturalLetterName (PitchClass::f()));
}

TEST_CASE("enharmonic spelling provides sharp and flat defaults", "[music_theory]")
{
    REQUIRE (spellPitchClass (PitchClass::aSharp(), SpellingPreference::preferSharps).toString() == "A#");
    REQUIRE (spellPitchClass (PitchClass::aSharp(), SpellingPreference::preferFlats).toString() == "Bb");

    const auto spellings = commonSpellingsFor (PitchClass::cSharp());
    REQUIRE (spellings.size() == 2);
    REQUIRE (spellings[0].toString() == "C#");
    REQUIRE (spellings[1].toString() == "Db");
}

TEST_CASE("midi pitch validates range and remains separate from spelling", "[music_theory]")
{
    REQUIRE_THROWS_AS (MidiPitch { -1 }, std::invalid_argument);
    REQUIRE_THROWS_AS (MidiPitch { 128 }, std::invalid_argument);

    const auto pitch = MidiPitch { 70 };
    REQUIRE (pitch.pitchClass() == NoteName::bFlat().pitchClass());
    REQUIRE (pitch.nameWithOctave (NoteName::bFlat()) == "Bb4");
    REQUIRE (pitch.nameWithOctave (NoteName::aSharp()) == "A#4");
}

TEST_CASE("scale degrees represent natural and altered degrees", "[music_theory]")
{
    REQUIRE (ScaleDegree::natural (1).toString() == "1");
    REQUIRE (ScaleDegree::flat (3).toString() == "b3");
    REQUIRE (ScaleDegree::sharp (4).toString() == "#4");
    REQUIRE (ScaleDegree::flat (7).toString() == "b7");

    REQUIRE (ScaleDegree::flat (3) == ScaleDegree { 3, -1 });
    REQUIRE (ScaleDegree::sharp (4).degree() == 4);
    REQUIRE (ScaleDegree::sharp (4).alteration() == 1);
}
