#include "core/music_theory/CustomScaleBuilder.h"
#include "core/music_theory/ScaleLibrary.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace tsq::core::music_theory;

ScaleMetadata metadataForTest()
{
    return ScaleMetadata {
        "Custom Bright Scale",
        "Custom",
        { "user", "bright", "favorite" },
        "A user-created C-based scale."
    };
}

std::vector<std::string> noteStringsFor (const ScaleInstance& scale)
{
    std::vector<std::string> result;

    for (const auto noteName : scale.visibleNoteSpellings())
        result.push_back (noteName.toString());

    return result;
}
}

TEST_CASE ("Custom scale builder creates a major-like pitch collection")
{
    const auto definition = CustomScaleBuilder::build (
        metadataForTest(),
        { PitchClass::c(), PitchClass::d(), PitchClass::e(), PitchClass::f(), PitchClass::g(), PitchClass::a(), PitchClass::b() });

    CHECK (definition.pitchClassOffsetsFromRoot() == std::vector<int> { 0, 2, 4, 5, 7, 9, 11 });
    CHECK (noteStringsFor (ScaleInstance { NoteName::c(), definition }) == std::vector<std::string> { "C", "D", "E", "F", "G", "A", "B" });
}

TEST_CASE ("Custom scale builder creates a whole-tone-like pitch collection")
{
    const auto definition = CustomScaleBuilder::build (
        ScaleMetadata { "Custom Whole Tone", "Custom", { "whole", "symmetric" }, "A user-created whole-tone collection." },
        { PitchClass::c(), PitchClass::d(), PitchClass::e(), PitchClass::fSharp(), PitchClass::gSharp(), PitchClass::aSharp() });

    CHECK (definition.pitchClassOffsetsFromRoot() == std::vector<int> { 0, 2, 4, 6, 8, 10 });
    CHECK (noteStringsFor (ScaleInstance { NoteName::c(), definition }) == std::vector<std::string> { "C", "D", "E", "F#", "G#", "A#" });
}

TEST_CASE ("Custom scale validation rejects missing C")
{
    const CustomScaleSpecification specification {
        metadataForTest(),
        { PitchClass::d(), PitchClass::e(), PitchClass::f() }
    };

    const auto validation = validateCustomScaleSpecification (specification);

    CHECK_FALSE (validation.isValid());
    CHECK (validation.hasIssue (CustomScaleValidationIssueCode::missingRootC));
    CHECK_THROWS_AS (CustomScaleBuilder::build (specification), std::invalid_argument);
}

TEST_CASE ("Custom scale validation rejects empty names and duplicate pitch classes")
{
    const CustomScaleSpecification specification {
        ScaleMetadata { "", "Custom", {}, "No name." },
        { PitchClass::c(), PitchClass::d(), PitchClass::d() }
    };

    const auto validation = validateCustomScaleSpecification (specification);

    CHECK_FALSE (validation.isValid());
    CHECK (validation.hasIssue (CustomScaleValidationIssueCode::emptyName));
    CHECK (validation.hasIssue (CustomScaleValidationIssueCode::duplicatePitchClass));
}

TEST_CASE ("Custom scale metadata is preserved and searchable from scale library")
{
    ScaleLibrary library;
    library.addDefinition (CustomScaleBuilder::build (metadataForTest(), { PitchClass::c(), PitchClass::e(), PitchClass::g() }));

    const auto* customScale = library.findByName ("Custom Bright Scale");
    REQUIRE (customScale != nullptr);

    CHECK (customScale->name() == "Custom Bright Scale");
    CHECK (customScale->category() == "Custom");
    CHECK (customScale->tags() == std::vector<std::string> { "user", "bright", "favorite" });
    CHECK (customScale->description() == "A user-created C-based scale.");

    const auto matches = library.search ("bright");

    REQUIRE (! matches.empty());
    CHECK (matches.front().name() == "Custom Bright Scale");
}
