#pragma once

#include "core/music_theory/MidiPitch.h"
#include "core/music_theory/NoteName.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/HarmonicContext.h"

#include <cstddef>

namespace tsq::core::sequencing
{
struct NoteHarmonicInterpretation
{
    HarmonicContext sourceContext { music_theory::PitchClass::c(), "Major" };
    std::size_t scaleDegreeIndex = 0;
    int alteration = 0;

    bool isAccidental() const noexcept;
};

bool operator== (const NoteHarmonicInterpretation& lhs, const NoteHarmonicInterpretation& rhs) noexcept;
bool operator!= (const NoteHarmonicInterpretation& lhs, const NoteHarmonicInterpretation& rhs) noexcept;

NoteHarmonicInterpretation interpretNoteHarmonically (music_theory::MidiPitch pitch,
                                                      const HarmonicContext& context,
                                                      const music_theory::ScaleLibrary& scaleLibrary);

music_theory::MidiPitch pitchForInterpretation (const NoteHarmonicInterpretation& interpretation,
                                                music_theory::MidiPitch currentPitch,
                                                const HarmonicContext& targetContext,
                                                const music_theory::ScaleLibrary& scaleLibrary);

music_theory::NoteName spellingForInterpretation (const NoteHarmonicInterpretation& interpretation,
                                                  music_theory::MidiPitch targetPitch,
                                                  const HarmonicContext& targetContext,
                                                  const music_theory::ScaleLibrary& scaleLibrary);

NoteHarmonicInterpretation retargetInterpretation (const NoteHarmonicInterpretation& interpretation,
                                                   const HarmonicContext& targetContext,
                                                   const music_theory::ScaleLibrary& scaleLibrary);
}
