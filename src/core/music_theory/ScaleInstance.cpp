#include "core/music_theory/ScaleInstance.h"

#include "core/music_theory/EnharmonicSpelling.h"

#include <utility>

namespace tsq::core::music_theory
{
namespace
{
int letterIndex (LetterName letter) noexcept
{
    switch (letter)
    {
        case LetterName::c: return 0;
        case LetterName::d: return 1;
        case LetterName::e: return 2;
        case LetterName::f: return 3;
        case LetterName::g: return 4;
        case LetterName::a: return 5;
        case LetterName::b: return 6;
    }

    return 0;
}

LetterName letterFromIndex (int index) noexcept
{
    switch ((index % 7 + 7) % 7)
    {
        case 0: return LetterName::c;
        case 1: return LetterName::d;
        case 2: return LetterName::e;
        case 3: return LetterName::f;
        case 4: return LetterName::g;
        case 5: return LetterName::a;
        case 6: return LetterName::b;
    }

    return LetterName::c;
}

LetterName degreeLetterFromRoot (LetterName root, int degree) noexcept
{
    return letterFromIndex (letterIndex (root) + degree - 1);
}

int centeredAccidentalOffset (PitchClass targetPitchClass, LetterName letter) noexcept
{
    auto offset = targetPitchClass.semitonesFromC() - NoteName { letter }.pitchClass().semitonesFromC();

    while (offset > 6)
        offset -= 12;

    while (offset < -6)
        offset += 12;

    return offset;
}
}

ScaleInstance::ScaleInstance (NoteName root, ScaleDefinition definition)
    : root_ (root),
      definition_ (std::move (definition))
{
}

const NoteName& ScaleInstance::root() const noexcept
{
    return root_;
}

const ScaleDefinition& ScaleInstance::definition() const noexcept
{
    return definition_;
}

std::vector<PitchClass> ScaleInstance::pitchClasses() const
{
    std::vector<PitchClass> result;
    result.reserve (definition_.pitchClassOffsetsFromRoot().size());

    for (const auto offset : definition_.pitchClassOffsetsFromRoot())
        result.push_back (root_.pitchClass().transposedBy (offset));

    return result;
}

bool ScaleInstance::contains (PitchClass pitchClass) const noexcept
{
    const auto offsetFromRoot = pitchClass.semitonesFromC() - root_.pitchClass().semitonesFromC();
    return definition_.containsOffsetFromRoot (offsetFromRoot);
}

std::vector<NoteName> ScaleInstance::visibleNoteSpellings() const
{
    const auto& offsets = definition_.pitchClassOffsetsFromRoot();
    const auto& degrees = definition_.degreeMapping();

    std::vector<NoteName> result;
    result.reserve (offsets.size());

    for (std::size_t index = 0; index < offsets.size(); ++index)
    {
        const auto pitchClass = root_.pitchClass().transposedBy (offsets[index]);
        const auto letter = degreeLetterFromRoot (root_.letter(), degrees[index].degree());
        const auto accidentalOffset = centeredAccidentalOffset (pitchClass, letter);

        if (accidentalOffset >= -2 && accidentalOffset <= 2)
            result.emplace_back (letter, Accidental { accidentalOffset });
        else
            result.push_back (spellPitchClass (pitchClass, definition_.preferredSpelling()));
    }

    return result;
}
}
