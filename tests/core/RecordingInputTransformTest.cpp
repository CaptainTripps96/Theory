#include "core/sequencing/RecordingInputTransform.h"

#include <catch2/catch_test_macros.hpp>

using namespace tsq::core::music_theory;
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

namespace
{
const auto cMajorContext = HarmonicContext { PitchClass::c(), "Major" };
const auto scaleLibrary = ScaleLibrary::createBuiltInLibrary();
}

TEST_CASE ("Input quantize snaps note starts to sixteenth grid")
{
    ProjectRhythmSettings settings;

    CHECK (quantizeInputStart (TickPosition::fromTicks (250), "sixteenth", settings).ticks() == 240);
    CHECK (quantizeInputStart (TickPosition::fromTicks (370), "sixteenth", settings).ticks() == 480);
}

TEST_CASE ("Quintuplet input quantize is unavailable until enabled")
{
    ProjectRhythmSettings settings;

    CHECK_FALSE (inputQuantizationDivisionById ("sixteenthQuintuplet", settings).has_value());

    settings.setQuintupletsEnabled (true);

    const auto quintuplet = inputQuantizationDivisionById ("sixteenthQuintuplet", settings);
    REQUIRE (quintuplet.has_value());
    CHECK (quintuplet->tickDuration.ticks() == 192);
}

TEST_CASE ("Scale Lock off records F sharp in C Major unchanged")
{
    const auto fSharp = MidiPitch::fromValue (66);

    CHECK (applyScaleLock (fSharp, cMajorContext, scaleLibrary, ScaleLockMode::off) == fSharp);
    CHECK (spellingForRecordedPitch (fSharp, cMajorContext, scaleLibrary) == NoteName::fSharp());
}

TEST_CASE ("Scale Lock nearest maps F sharp in C Major upward on tie")
{
    const auto fSharp = MidiPitch::fromValue (66);

    CHECK (applyScaleLock (fSharp, cMajorContext, scaleLibrary, ScaleLockMode::nearest) == MidiPitch::fromValue (67));
    CHECK (spellingForRecordedPitch (MidiPitch::fromValue (67), cMajorContext, scaleLibrary) == NoteName::g());
}

TEST_CASE ("Scale Lock round up and down map F sharp in C Major predictably")
{
    const auto fSharp = MidiPitch::fromValue (66);

    CHECK (applyScaleLock (fSharp, cMajorContext, scaleLibrary, ScaleLockMode::roundUp) == MidiPitch::fromValue (67));
    CHECK (applyScaleLock (fSharp, cMajorContext, scaleLibrary, ScaleLockMode::roundDown) == MidiPitch::fromValue (65));
}
