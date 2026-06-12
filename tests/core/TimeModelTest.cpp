#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/time/GridDivision.h"
#include "core/time/TempoMap.h"
#include "core/time/TimeSignatureMap.h"
#include "core/time/Tuplet.h"

#include <algorithm>
#include <cmath>

using namespace tsq::core::time;

TEST_CASE("time model uses 960 PPQ", "[time]")
{
    REQUIRE (ticksPerQuarterNote == 960);
    REQUIRE (quarterNoteDuration().ticks() == 960);
    REQUIRE (eighthNoteDuration().ticks() == 480);
    REQUIRE (sixteenthNoteDuration().ticks() == 240);
    REQUIRE (durationFromQuarterNotes (1.5).ticks() == 1440);
    REQUIRE (quarterNotesFromDuration (TickDuration::fromTicks (1440)) == Catch::Approx (1.5));
    REQUIRE (isValid (BarBeatPosition { 1, 1, TickDuration {} }));
}

TEST_CASE("grid divisions expose standard tick durations", "[time]")
{
    REQUIRE (duration (GridDivision::quarter).ticks() == 960);
    REQUIRE (duration (GridDivision::eighth).ticks() == 480);
    REQUIRE (duration (GridDivision::sixteenth).ticks() == 240);
    REQUIRE (duration (GridDivision::thirtySecond).ticks() == 120);
}

TEST_CASE("disabled tuplets are absent from available grid divisions", "[time]")
{
    ProjectRhythmSettings settings;
    settings.setTripletsEnabled (false);

    const auto divisions = availableGridDivisions (settings);
    const auto hasEighthTriplet = std::any_of (divisions.begin(), divisions.end(), [] (const auto& division) {
        return division.id == "eighthTriplet";
    });

    REQUIRE_FALSE (hasEighthTriplet);
}

TEST_CASE("enabled quintuplets appear in available grid divisions", "[time]")
{
    ProjectRhythmSettings settings;
    settings.setQuintupletsEnabled (true);

    const auto divisions = availableGridDivisions (settings);
    const auto hasSixteenthQuintuplet = std::any_of (divisions.begin(), divisions.end(), [] (const auto& division) {
        return division.id == "sixteenthQuintuplet";
    });

    REQUIRE (hasSixteenthQuintuplet);
    REQUIRE (gridDivisionDefinitionFor ("sixteenthQuintuplet", settings).tickDuration.ticks() == 192);
}

TEST_CASE("time signatures calculate bar length", "[time]")
{
    REQUIRE (TimeSignature { 4, 4 }.barDuration().ticks() == 3840);
    REQUIRE (TimeSignature { 3, 4 }.barDuration().ticks() == 2880);
    REQUIRE (TimeSignature { 7, 8 }.barDuration().ticks() == 3360);
}

TEST_CASE("fixed tempo converts between ticks and seconds", "[time]")
{
    TempoMap tempoMap { Tempo { 120.0 } };

    REQUIRE (tempoMap.secondsAt (TickPosition::fromTicks (quarterNoteDuration().ticks())) == Catch::Approx (0.5));
    REQUIRE (tempoMap.secondsAt (TickPosition::fromTicks (TimeSignature { 4, 4 }.barDuration().ticks())) == Catch::Approx (2.0));
    REQUIRE (tempoMap.tickAtSeconds (0.5).ticks() == 960);
    REQUIRE (tempoMap.durationToSeconds (TickPosition::fromTicks (960), TickDuration::fromTicks (480)) == Catch::Approx (0.25));
}

TEST_CASE("tempo map linearly interpolates BPM between nodes", "[time]")
{
    TempoMap tempoMap { Tempo { 120.0 } };
    tempoMap.addNode (TickPosition::fromTicks (960), Tempo { 140.0 });

    REQUIRE (tempoMap.tempoAt (TickPosition::fromTicks (480)).bpm() == Catch::Approx (130.0));

    const auto expectedRampSeconds = 3.0 * std::log (140.0 / 120.0);
    REQUIRE (tempoMap.secondsAt (TickPosition::fromTicks (960)) == Catch::Approx (expectedRampSeconds).margin (0.000000001));
}

TEST_CASE("time signature map converts across marker changes", "[time]")
{
    TimeSignatureMap signatureMap { TimeSignature { 4, 4 } };
    const auto barThreeStart = TickPosition::fromTicks (2 * TimeSignature { 4, 4 }.barDuration().ticks());
    signatureMap.addMarker (barThreeStart, TimeSignature { 3, 4 });

    REQUIRE (signatureMap.tickAtBar (1).ticks() == 0);
    REQUIRE (signatureMap.tickAtBar (2).ticks() == 3840);
    REQUIRE (signatureMap.tickAtBar (3).ticks() == 7680);
    REQUIRE (signatureMap.tickAtBar (4).ticks() == 10560);

    const auto barThreeBeatTwo = signatureMap.toTicks (BarBeatPosition { 3, 2, TickDuration {} });
    REQUIRE (barThreeBeatTwo.ticks() == 8640);

    const auto converted = signatureMap.fromTicks (barThreeBeatTwo + TickDuration::fromTicks (240));
    REQUIRE (converted.bar == 3);
    REQUIRE (converted.beat == 2);
    REQUIRE (converted.tickOffset.ticks() == 240);
}

TEST_CASE("time signature map preserves bar beat display across multiple meters", "[time]")
{
    TimeSignatureMap signatureMap { TimeSignature { 4, 4 } };
    const auto barThreeStart = TickPosition::fromTicks (2 * TimeSignature { 4, 4 }.barDuration().ticks());
    signatureMap.addMarker (barThreeStart, TimeSignature { 3, 4 });
    signatureMap.addMarker (TickPosition::fromTicks (barThreeStart.ticks() + (2 * TimeSignature { 3, 4 }.barDuration().ticks())),
                            TimeSignature { 7, 8 });

    CHECK (signatureMap.fromTicks (TickPosition::fromTicks (0)).bar == 1);
    CHECK (signatureMap.fromTicks (barThreeStart).bar == 3);

    const auto barFiveStart = signatureMap.tickAtBar (5);
    REQUIRE (signatureMap.timeSignatureAt (barFiveStart).numerator() == 7);
    REQUIRE (signatureMap.timeSignatureAt (barFiveStart).denominator() == 8);

    const auto barFiveBeatSeven = signatureMap.toTicks (BarBeatPosition { 5, 7, TickDuration {} });
    const auto converted = signatureMap.fromTicks (barFiveBeatSeven + TickDuration::fromTicks (120));
    CHECK (converted.bar == 5);
    CHECK (converted.beat == 7);
    CHECK (converted.tickOffset.ticks() == 120);
}

TEST_CASE("tuplets calculate note duration", "[time]")
{
    Tuplet quarterNoteQuintuplet { quarterNoteDuration(), 5, 4 };

    REQUIRE (quarterNoteQuintuplet.totalDuration().ticks() == 3840);
    REQUIRE (quarterNoteQuintuplet.noteDurationTicksExact() == Catch::Approx (768.0));
    REQUIRE (quarterNoteQuintuplet.noteDuration().ticks() == 768);
}
