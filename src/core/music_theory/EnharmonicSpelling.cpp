#include "core/music_theory/EnharmonicSpelling.h"

namespace tsq::core::music_theory
{
NoteName spellPitchClass (PitchClass pitchClass, SpellingPreference preference)
{
    switch (pitchClass.semitonesFromC())
    {
        case 0: return NoteName::c();
        case 1: return preference == SpellingPreference::preferFlats ? NoteName::dFlat() : NoteName::cSharp();
        case 2: return NoteName::d();
        case 3: return preference == SpellingPreference::preferFlats ? NoteName::eFlat() : NoteName { LetterName::d, Accidental::sharp() };
        case 4: return NoteName::e();
        case 5: return NoteName::f();
        case 6: return preference == SpellingPreference::preferFlats ? NoteName::gFlat() : NoteName::fSharp();
        case 7: return NoteName::g();
        case 8: return preference == SpellingPreference::preferFlats ? NoteName::aFlat() : NoteName { LetterName::g, Accidental::sharp() };
        case 9: return NoteName::a();
        case 10: return preference == SpellingPreference::preferFlats ? NoteName::bFlat() : NoteName::aSharp();
        case 11: return NoteName::b();
    }

    return NoteName::c();
}

std::vector<NoteName> commonSpellingsFor (PitchClass pitchClass)
{
    switch (pitchClass.semitonesFromC())
    {
        case 0: return { NoteName::c() };
        case 1: return { NoteName::cSharp(), NoteName::dFlat() };
        case 2: return { NoteName::d() };
        case 3: return { NoteName { LetterName::d, Accidental::sharp() }, NoteName::eFlat() };
        case 4: return { NoteName::e() };
        case 5: return { NoteName::f() };
        case 6: return { NoteName::fSharp(), NoteName::gFlat() };
        case 7: return { NoteName::g() };
        case 8: return { NoteName { LetterName::g, Accidental::sharp() }, NoteName::aFlat() };
        case 9: return { NoteName::a() };
        case 10: return { NoteName::aSharp(), NoteName::bFlat() };
        case 11: return { NoteName::b() };
    }

    return { NoteName::c() };
}
}
