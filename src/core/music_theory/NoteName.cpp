#include "core/music_theory/NoteName.h"

#include <stdexcept>

namespace tsq::core::music_theory
{
namespace
{
int naturalSemitonesFromC (LetterName letter) noexcept
{
    switch (letter)
    {
        case LetterName::c: return 0;
        case LetterName::d: return 2;
        case LetterName::e: return 4;
        case LetterName::f: return 5;
        case LetterName::g: return 7;
        case LetterName::a: return 9;
        case LetterName::b: return 11;
    }

    return 0;
}
}

Accidental::Accidental (int semitoneOffset)
    : semitoneOffset_ (semitoneOffset)
{
    if (semitoneOffset_ < -2 || semitoneOffset_ > 2)
        throw std::invalid_argument ("Accidental semitone offset must be between -2 and 2");
}

Accidental Accidental::flat()
{
    return Accidental { -1 };
}

Accidental Accidental::natural()
{
    return Accidental {};
}

Accidental Accidental::sharp()
{
    return Accidental { 1 };
}

Accidental Accidental::doubleFlat()
{
    return Accidental { -2 };
}

Accidental Accidental::doubleSharp()
{
    return Accidental { 2 };
}

int Accidental::semitoneOffset() const noexcept
{
    return semitoneOffset_;
}

std::string Accidental::toString() const
{
    switch (semitoneOffset_)
    {
        case -2: return "bb";
        case -1: return "b";
        case 0: return "";
        case 1: return "#";
        case 2: return "##";
    }

    return "";
}

NoteName::NoteName (LetterName letter, Accidental accidental)
    : letter_ (letter),
      accidental_ (accidental)
{
}

NoteName NoteName::c() { return NoteName { LetterName::c }; }
NoteName NoteName::cSharp() { return NoteName { LetterName::c, Accidental::sharp() }; }
NoteName NoteName::dFlat() { return NoteName { LetterName::d, Accidental::flat() }; }
NoteName NoteName::d() { return NoteName { LetterName::d }; }
NoteName NoteName::eFlat() { return NoteName { LetterName::e, Accidental::flat() }; }
NoteName NoteName::e() { return NoteName { LetterName::e }; }
NoteName NoteName::f() { return NoteName { LetterName::f }; }
NoteName NoteName::fSharp() { return NoteName { LetterName::f, Accidental::sharp() }; }
NoteName NoteName::gFlat() { return NoteName { LetterName::g, Accidental::flat() }; }
NoteName NoteName::g() { return NoteName { LetterName::g }; }
NoteName NoteName::aFlat() { return NoteName { LetterName::a, Accidental::flat() }; }
NoteName NoteName::a() { return NoteName { LetterName::a }; }
NoteName NoteName::bFlat() { return NoteName { LetterName::b, Accidental::flat() }; }
NoteName NoteName::aSharp() { return NoteName { LetterName::a, Accidental::sharp() }; }
NoteName NoteName::b() { return NoteName { LetterName::b }; }

LetterName NoteName::letter() const noexcept
{
    return letter_;
}

Accidental NoteName::accidental() const noexcept
{
    return accidental_;
}

PitchClass NoteName::pitchClass() const noexcept
{
    return PitchClass::fromSemitonesFromC (naturalSemitonesFromC (letter_) + accidental_.semitoneOffset());
}

std::string NoteName::toString() const
{
    return tsq::core::music_theory::toString (letter_) + accidental_.toString();
}

bool operator== (Accidental lhs, Accidental rhs) noexcept
{
    return lhs.semitoneOffset() == rhs.semitoneOffset();
}

bool operator!= (Accidental lhs, Accidental rhs) noexcept
{
    return ! (lhs == rhs);
}

bool operator== (NoteName lhs, NoteName rhs) noexcept
{
    return lhs.letter() == rhs.letter() && lhs.accidental() == rhs.accidental();
}

bool operator!= (NoteName lhs, NoteName rhs) noexcept
{
    return ! (lhs == rhs);
}

std::string toString (LetterName letter)
{
    switch (letter)
    {
        case LetterName::c: return "C";
        case LetterName::d: return "D";
        case LetterName::e: return "E";
        case LetterName::f: return "F";
        case LetterName::g: return "G";
        case LetterName::a: return "A";
        case LetterName::b: return "B";
    }

    return "C";
}
}
