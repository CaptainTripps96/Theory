#include "core/commands/AddScaleModeRegionCommand.h"
#include "core/commands/CommandStack.h"
#include "core/sequencing/BorrowedChordAnalysis.h"
#include "core/sequencing/Project.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace tsq::core;
using namespace tsq::core::commands;
using namespace tsq::core::music_theory;
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

TickPosition beat (int zeroBasedBeat)
{
    return TickPosition::fromTicks (static_cast<std::int64_t> (zeroBasedBeat) * ticksPerQuarterNote);
}

Region bars (int firstBar, int onePastLastBar)
{
    return Region { beat ((firstBar - 1) * 4), beat ((onePastLastBar - 1) * 4) };
}

ChordRegion bFlatMajorRegion()
{
    return ChordRegion {
        bars (1, 3),
        PitchClass::aSharp(),
        ChordQuality::major,
        { PitchClass::aSharp(), PitchClass::d(), PitchClass::f() },
        "Bb"
    };
}

bool hasScaleSuggestion (const std::vector<CompatibleScaleSuggestion>& suggestions, const std::string& scaleName)
{
    for (const auto& suggestion : suggestions)
    {
        if (suggestion.context.scaleDefinitionName() == scaleName)
            return true;
    }

    return false;
}
}

TEST_CASE ("Bb chord in C Major is borrowed")
{
    MusicalStructure structure;
    structure.addChordRegion (bFlatMajorRegion());

    CHECK (isBorrowedChordRegion (
        structure,
        structure.chordRegions()[0],
        ScaleLibrary::createBuiltInLibrary()));
}

TEST_CASE ("Bb chord in C Mixolydian is diatonic")
{
    MusicalStructure structure;
    structure.addScaleModeRegion (ScaleModeRegion { bars (1, 3), "Mixolydian" });
    structure.addChordRegion (bFlatMajorRegion());

    CHECK_FALSE (isBorrowedChordRegion (
        structure,
        structure.chordRegions()[0],
        ScaleLibrary::createBuiltInLibrary()));
}

TEST_CASE ("Compatible mode search for Bb in C suggests Mixolydian")
{
    MusicalStructure structure;
    structure.addChordRegion (bFlatMajorRegion());

    const auto suggestions = compatibleScaleSuggestionsFor (
        structure,
        structure.chordRegions()[0],
        ScaleLibrary::createBuiltInLibrary());

    CHECK (hasScaleSuggestion (suggestions, "Mixolydian"));
}

TEST_CASE ("Applying compatible mode suggestion adds scale region and supports undo redo")
{
    Project project { "project-1", "Song" };
    project.musicalStructure().addChordRegion (bFlatMajorRegion());
    const auto suggestions = compatibleScaleSuggestionsFor (
        project.musicalStructure(),
        project.musicalStructure().chordRegions()[0],
        ScaleLibrary::createBuiltInLibrary());

    REQUIRE (hasScaleSuggestion (suggestions, "Mixolydian"));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddScaleModeRegionCommand> (
        ScaleModeRegion { project.musicalStructure().chordRegions()[0].region(), "Mixolydian" })).succeeded());
    REQUIRE (project.musicalStructure().scaleModeRegions().size() == 1);
    CHECK (project.musicalStructure().scaleModeRegions()[0].scaleDefinitionName() == "Mixolydian");
    CHECK_FALSE (isBorrowedChordRegion (
        project.musicalStructure(),
        project.musicalStructure().chordRegions()[0],
        ScaleLibrary::createBuiltInLibrary()));

    REQUIRE (stack.undo().succeeded());
    CHECK (project.musicalStructure().scaleModeRegions().empty());

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.musicalStructure().scaleModeRegions().size() == 1);
    CHECK (project.musicalStructure().scaleModeRegions()[0].scaleDefinitionName() == "Mixolydian");
}
