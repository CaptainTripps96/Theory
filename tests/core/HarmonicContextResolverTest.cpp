#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/HarmonicContextResolver.h"
#include "core/time/Tick.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace tsq::core;
using namespace tsq::core::music_theory;
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

TickPosition barStart (int oneBasedBar)
{
    return TickPosition::fromTicks (static_cast<std::int64_t> (oneBasedBar - 1) * 4 * ticksPerQuarterNote);
}

Region bars (int firstBar, int onePastLastBar)
{
    return Region { barStart (firstBar), barStart (onePastLastBar) };
}

std::vector<std::string> noteStringsFor (const ScaleInstance& scale)
{
    std::vector<std::string> result;

    for (const auto noteName : scale.visibleNoteSpellings())
        result.push_back (noteName.toString());

    return result;
}
}

TEST_CASE ("Harmonic context resolver resolves C Major at bar one by default")
{
    const MusicalStructure structure;
    const HarmonicContextResolver resolver { structure };
    const ScaleLibrary scales;

    const auto context = resolver.resolveAt (barStart (1));

    CHECK (context.keyCenter() == PitchClass::c());
    CHECK (context.scaleDefinitionName() == "Major");
    CHECK (noteStringsFor (context.scaleInstance (scales)) == std::vector<std::string> { "C", "D", "E", "F", "G", "A", "B" });
}

TEST_CASE ("Harmonic context resolver resolves C Mixolydian after scale region change")
{
    MusicalStructure structure;
    structure.addScaleModeRegion (ScaleModeRegion { bars (5, 9), "Mixolydian" });

    const HarmonicContextResolver resolver { structure };
    const ScaleLibrary scales;
    const auto context = resolver.resolveAt (barStart (5));

    CHECK (context.keyCenter() == PitchClass::c());
    CHECK (context.scaleDefinitionName() == "Mixolydian");
    CHECK (noteStringsFor (context.scaleInstance (scales)) == std::vector<std::string> { "C", "D", "E", "F", "G", "A", "Bb" });
}

TEST_CASE ("Key center and scale mode lanes change independently")
{
    MusicalStructure structure;
    structure.addKeyCenterRegion (KeyCenterRegion { bars (3, 7), PitchClass::g() });
    structure.addScaleModeRegion (ScaleModeRegion { bars (5, 9), "Mixolydian" });

    const HarmonicContextResolver resolver { structure };

    const auto barThree = resolver.resolveAt (barStart (3));
    CHECK (barThree.keyCenter() == PitchClass::g());
    CHECK (barThree.scaleDefinitionName() == "Major");

    const auto barFive = resolver.resolveAt (barStart (5));
    CHECK (barFive.keyCenter() == PitchClass::g());
    CHECK (barFive.scaleDefinitionName() == "Mixolydian");

    const auto barSeven = resolver.resolveAt (barStart (7));
    CHECK (barSeven.keyCenter() == PitchClass::c());
    CHECK (barSeven.scaleDefinitionName() == "Mixolydian");
}

TEST_CASE ("Clip range spanning harmonic regions resolves each context segment")
{
    MusicalStructure structure;
    structure.addScaleModeRegion (ScaleModeRegion { bars (1, 5), "Major" });
    structure.addScaleModeRegion (ScaleModeRegion { bars (5, 9), "Natural Minor" });

    const HarmonicContextResolver resolver { structure };
    const auto segments = resolver.resolveRange (bars (1, 9));

    REQUIRE (segments.size() == 2);

    CHECK (segments[0].region.start() == barStart (1));
    CHECK (segments[0].region.end() == barStart (5));
    CHECK (segments[0].context.keyCenter() == PitchClass::c());
    CHECK (segments[0].context.scaleDefinitionName() == "Major");

    CHECK (segments[1].region.start() == barStart (5));
    CHECK (segments[1].region.end() == barStart (9));
    CHECK (segments[1].context.keyCenter() == PitchClass::c());
    CHECK (segments[1].context.scaleDefinitionName() == "Natural Minor");
}

TEST_CASE ("Region boundary behavior is deterministic and half-open")
{
    MusicalStructure structure;
    structure.addScaleModeRegion (ScaleModeRegion { bars (1, 5), "Major" });
    structure.addScaleModeRegion (ScaleModeRegion { bars (5, 9), "Mixolydian" });

    const HarmonicContextResolver resolver { structure };

    CHECK (resolver.resolveAt (barStart (1)).scaleDefinitionName() == "Major");
    CHECK (resolver.resolveAt (barStart (5) - TickDuration::fromTicks (1)).scaleDefinitionName() == "Major");
    CHECK (resolver.resolveAt (barStart (5)).scaleDefinitionName() == "Mixolydian");
    CHECK (resolver.resolveAt (barStart (9)).scaleDefinitionName() == "Major");
}

TEST_CASE ("Regions reject negative durations")
{
    CHECK_THROWS_AS (Region (barStart (3), barStart (2)), std::invalid_argument);
}
