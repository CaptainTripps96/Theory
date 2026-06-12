#pragma once

#include "core/music_theory/PitchClass.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/MusicalStructure.h"
#include "core/time/Tick.h"

namespace tsq::core::sequencing
{
enum class HarmonicOverlayRole
{
    none,
    root,
    chordTone,
    nonChordScaleTone,
    accidental
};

HarmonicOverlayRole harmonicOverlayRoleAt (const MusicalStructure& structure,
                                           const music_theory::ScaleLibrary& scaleLibrary,
                                           time::TickPosition projectPosition,
                                           music_theory::PitchClass pitchClass);
}
