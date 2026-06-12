#include "core/sequencing/HarmonicOverlay.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace tsq::core;
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

ChordRegion chordRegion (Region region,
                         PitchClass root,
                         ChordQuality quality,
                         std::vector<PitchClass> tones,
                         std::string name)
{
    return ChordRegion { region, root, quality, std::move (tones), std::move (name) };
}

HarmonicOverlayRole roleAt (const MusicalStructure& structure, TickPosition position, PitchClass pitchClass)
{
    return harmonicOverlayRoleAt (structure, ScaleLibrary::createBuiltInLibrary(), position, pitchClass);
}
}

TEST_CASE ("Harmonic overlay classifies C major chord tones over C major scale")
{
    MusicalStructure structure;
    structure.addChordRegion (chordRegion (
        bars (1, 3),
        PitchClass::c(),
        ChordQuality::major,
        { PitchClass::c(), PitchClass::e(), PitchClass::g() },
        "C"));

    CHECK (roleAt (structure, beat (0), PitchClass::c()) == HarmonicOverlayRole::root);
    CHECK (roleAt (structure, beat (0), PitchClass::e()) == HarmonicOverlayRole::chordTone);
    CHECK (roleAt (structure, beat (0), PitchClass::g()) == HarmonicOverlayRole::chordTone);
}

TEST_CASE ("Harmonic overlay follows a D minor region after C major")
{
    MusicalStructure structure;
    structure.addChordRegion (chordRegion (
        bars (1, 3),
        PitchClass::c(),
        ChordQuality::major,
        { PitchClass::c(), PitchClass::e(), PitchClass::g() },
        "C"));
    structure.addChordRegion (chordRegion (
        bars (3, 5),
        PitchClass::d(),
        ChordQuality::minor,
        { PitchClass::d(), PitchClass::f(), PitchClass::a() },
        "Dm"));

    CHECK (roleAt (structure, beat (0), PitchClass::c()) == HarmonicOverlayRole::root);
    CHECK (roleAt (structure, beat (8), PitchClass::d()) == HarmonicOverlayRole::root);
    CHECK (roleAt (structure, beat (8), PitchClass::f()) == HarmonicOverlayRole::chordTone);
    CHECK (roleAt (structure, beat (8), PitchClass::c()) == HarmonicOverlayRole::nonChordScaleTone);
}

TEST_CASE ("Harmonic overlay identifies non-chord scale tones")
{
    MusicalStructure structure;
    structure.addChordRegion (chordRegion (
        bars (1, 3),
        PitchClass::c(),
        ChordQuality::major,
        { PitchClass::c(), PitchClass::e(), PitchClass::g() },
        "C"));

    CHECK (roleAt (structure, beat (0), PitchClass::d()) == HarmonicOverlayRole::nonChordScaleTone);
    CHECK (roleAt (structure, beat (0), PitchClass::a()) == HarmonicOverlayRole::nonChordScaleTone);
}

TEST_CASE ("Harmonic overlay gives accidentals precedence over chord tones")
{
    MusicalStructure structure;
    structure.addChordRegion (chordRegion (
        bars (1, 3),
        PitchClass::aSharp(),
        ChordQuality::major,
        { PitchClass::aSharp(), PitchClass::d(), PitchClass::f() },
        "Bb"));

    CHECK (roleAt (structure, beat (0), PitchClass::aSharp()) == HarmonicOverlayRole::accidental);
    CHECK (roleAt (structure, beat (0), PitchClass::d()) == HarmonicOverlayRole::chordTone);
}

TEST_CASE ("Harmonic overlay includes chord extensions from stored chord tones")
{
    MusicalStructure structure;
    structure.addChordRegion (chordRegion (
        bars (1, 3),
        PitchClass::c(),
        ChordQuality::majorSeventh,
        { PitchClass::c(), PitchClass::e(), PitchClass::g(), PitchClass::b() },
        "Cmaj7"));

    CHECK (roleAt (structure, beat (0), PitchClass::b()) == HarmonicOverlayRole::chordTone);
}

TEST_CASE ("Harmonic overlay is empty outside chord regions")
{
    MusicalStructure structure;
    CHECK (roleAt (structure, beat (0), PitchClass::c()) == HarmonicOverlayRole::none);
}
