#include "core/music_theory/ScaleLibrary.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
using namespace tsq::core::music_theory;

std::vector<std::string> noteStringsFor (const ScaleInstance& scale)
{
    std::vector<std::string> result;

    for (const auto noteName : scale.visibleNoteSpellings())
        result.push_back (noteName.toString());

    return result;
}
}

TEST_CASE ("Scale library builds common mode spellings from scale degree context")
{
    const ScaleLibrary library;

    CHECK (noteStringsFor (library.instantiate ("Major", NoteName::c())) == std::vector<std::string> { "C", "D", "E", "F", "G", "A", "B" });
    CHECK (noteStringsFor (library.instantiate ("Mixolydian", NoteName::c())) == std::vector<std::string> { "C", "D", "E", "F", "G", "A", "Bb" });
    CHECK (noteStringsFor (library.instantiate ("Major", NoteName::f())) == std::vector<std::string> { "F", "G", "A", "Bb", "C", "D", "E" });
    CHECK (noteStringsFor (library.instantiate ("Major", NoteName::e())) == std::vector<std::string> { "E", "F#", "G#", "A", "B", "C#", "D#" });
    CHECK (noteStringsFor (library.instantiate ("Dorian", NoteName::d())) == std::vector<std::string> { "D", "E", "F", "G", "A", "B", "C" });
}

TEST_CASE ("Scale library can search by metadata text")
{
    const ScaleLibrary library;

    REQUIRE (library.findByName ("Ionian") != nullptr);
    CHECK (library.findByName ("Ionian")->name() == "Major");

    const auto matches = library.search ("mix");

    REQUIRE (! matches.empty());
    CHECK (matches.front().name() == "Mixolydian");
}

TEST_CASE ("Scale instances can answer pitch-class membership")
{
    const ScaleLibrary library;
    const auto chromatic = library.instantiate ("Chromatic", NoteName::c());

    for (auto semitone = 0; semitone < 12; ++semitone)
        CHECK (chromatic.contains (PitchClass::fromSemitonesFromC (semitone)));

    const auto cMajor = library.instantiate ("Major", NoteName::c());

    CHECK (cMajor.contains (PitchClass::c()));
    CHECK (cMajor.contains (PitchClass::f()));
    CHECK_FALSE (cMajor.contains (PitchClass::fSharp()));
}
