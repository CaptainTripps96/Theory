#include "core/sequencing/NoteHarmonicInterpretation.h"

#include "core/music_theory/EnharmonicSpelling.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tsq::core::sequencing
{
namespace
{
int positiveModulo (int value, int modulus) noexcept
{
    const auto result = value % modulus;
    return result < 0 ? result + modulus : result;
}

int centeredPitchClassDelta (music_theory::PitchClass from, music_theory::PitchClass to) noexcept
{
    auto delta = to.semitonesFromC() - from.semitonesFromC();
    while (delta > 6)
        delta -= 12;
    while (delta < -6)
        delta += 12;
    return delta;
}

int pitchClassIntervalFromRoot (music_theory::PitchClass root, music_theory::PitchClass pitchClass) noexcept
{
    return positiveModulo (pitchClass.semitonesFromC() - root.semitonesFromC(), 12);
}

int rootMidiAtOrBelow (int midiPitch, music_theory::PitchClass root) noexcept
{
    const auto delta = positiveModulo (midiPitch - root.semitonesFromC(), 12);
    return midiPitch - delta;
}

music_theory::NoteName transposedSpelling (music_theory::NoteName baseSpelling, int alteration)
{
    return music_theory::NoteName {
        baseSpelling.letter(),
        music_theory::Accidental { baseSpelling.accidental().semitoneOffset() + alteration }
    };
}

std::size_t clampedDegreeIndex (std::size_t index, std::size_t scaleSize) noexcept
{
    if (scaleSize == 0)
        return 0;

    return std::min (index, scaleSize - 1);
}
}

bool NoteHarmonicInterpretation::isAccidental() const noexcept
{
    return alteration != 0;
}

bool operator== (const NoteHarmonicInterpretation& lhs, const NoteHarmonicInterpretation& rhs) noexcept
{
    return lhs.sourceContext == rhs.sourceContext
        && lhs.scaleDegreeIndex == rhs.scaleDegreeIndex
        && lhs.alteration == rhs.alteration;
}

bool operator!= (const NoteHarmonicInterpretation& lhs, const NoteHarmonicInterpretation& rhs) noexcept
{
    return ! (lhs == rhs);
}

NoteHarmonicInterpretation interpretNoteHarmonically (music_theory::MidiPitch pitch,
                                                      const HarmonicContext& context,
                                                      const music_theory::ScaleLibrary& scaleLibrary)
{
    const auto scale = context.scaleInstance (scaleLibrary);
    const auto pitchClasses = scale.pitchClasses();

    auto bestIndex = std::size_t { 0 };
    auto bestAlteration = 0;
    auto bestDistance = std::numeric_limits<int>::max();

    for (std::size_t index = 0; index < pitchClasses.size(); ++index)
    {
        const auto alteration = centeredPitchClassDelta (pitchClasses[index], pitch.pitchClass());
        const auto distance = std::abs (alteration);
        if (distance < bestDistance)
        {
            bestIndex = index;
            bestAlteration = alteration;
            bestDistance = distance;
        }
    }

    return NoteHarmonicInterpretation { context, bestIndex, bestAlteration };
}

music_theory::MidiPitch pitchForInterpretation (const NoteHarmonicInterpretation& interpretation,
                                                music_theory::MidiPitch currentPitch,
                                                const HarmonicContext& targetContext,
                                                const music_theory::ScaleLibrary& scaleLibrary)
{
    const auto targetScale = targetContext.scaleInstance (scaleLibrary);
    const auto targetPitchClasses = targetScale.pitchClasses();
    const auto degreeIndex = clampedDegreeIndex (interpretation.scaleDegreeIndex, targetPitchClasses.size());

    const auto sourceRootMidi = rootMidiAtOrBelow (currentPitch.value(), interpretation.sourceContext.keyCenter());
    const auto targetRootMidi = sourceRootMidi
        + centeredPitchClassDelta (interpretation.sourceContext.keyCenter(), targetContext.keyCenter());
    const auto targetInterval = pitchClassIntervalFromRoot (targetContext.keyCenter(), targetPitchClasses[degreeIndex]);

    return music_theory::MidiPitch::fromValue (targetRootMidi + targetInterval + interpretation.alteration);
}

music_theory::NoteName spellingForInterpretation (const NoteHarmonicInterpretation& interpretation,
                                                  music_theory::MidiPitch targetPitch,
                                                  const HarmonicContext& targetContext,
                                                  const music_theory::ScaleLibrary& scaleLibrary)
{
    const auto targetScale = targetContext.scaleInstance (scaleLibrary);
    const auto targetSpellings = targetScale.visibleNoteSpellings();
    const auto degreeIndex = clampedDegreeIndex (interpretation.scaleDegreeIndex, targetSpellings.size());

    try
    {
        return transposedSpelling (targetSpellings[degreeIndex], interpretation.alteration);
    }
    catch (...)
    {
        return music_theory::spellPitchClass (targetPitch.pitchClass(), targetScale.definition().preferredSpelling());
    }
}

NoteHarmonicInterpretation retargetInterpretation (const NoteHarmonicInterpretation& interpretation,
                                                   const HarmonicContext& targetContext,
                                                   const music_theory::ScaleLibrary& scaleLibrary)
{
    const auto targetScale = targetContext.scaleInstance (scaleLibrary);
    return NoteHarmonicInterpretation {
        targetContext,
        clampedDegreeIndex (interpretation.scaleDegreeIndex, targetScale.pitchClasses().size()),
        interpretation.alteration
    };
}
}
