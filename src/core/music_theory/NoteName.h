#pragma once

#include "core/music_theory/PitchClass.h"

#include <string>

namespace tsq::core::music_theory
{
enum class LetterName
{
    c,
    d,
    e,
    f,
    g,
    a,
    b
};

class Accidental
{
public:
    explicit Accidental (int semitoneOffset = 0);

    static Accidental flat();
    static Accidental natural();
    static Accidental sharp();
    static Accidental doubleFlat();
    static Accidental doubleSharp();

    int semitoneOffset() const noexcept;
    std::string toString() const;

private:
    int semitoneOffset_ = 0;
};

class NoteName
{
public:
    NoteName (LetterName letter, Accidental accidental = Accidental::natural());

    static NoteName c();
    static NoteName cSharp();
    static NoteName dFlat();
    static NoteName d();
    static NoteName eFlat();
    static NoteName e();
    static NoteName f();
    static NoteName fSharp();
    static NoteName gFlat();
    static NoteName g();
    static NoteName aFlat();
    static NoteName a();
    static NoteName bFlat();
    static NoteName aSharp();
    static NoteName b();

    LetterName letter() const noexcept;
    Accidental accidental() const noexcept;
    PitchClass pitchClass() const noexcept;
    std::string toString() const;

private:
    LetterName letter_ = LetterName::c;
    Accidental accidental_ {};
};

bool operator== (Accidental lhs, Accidental rhs) noexcept;
bool operator!= (Accidental lhs, Accidental rhs) noexcept;
bool operator== (NoteName lhs, NoteName rhs) noexcept;
bool operator!= (NoteName lhs, NoteName rhs) noexcept;

std::string toString (LetterName letter);
}
