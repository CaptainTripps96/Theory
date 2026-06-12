#pragma once

namespace tsq::core::music_theory
{
class PitchClass
{
public:
    constexpr PitchClass() noexcept = default;
    explicit constexpr PitchClass (int semitonesFromC) noexcept
        : semitonesFromC_ (normalize (semitonesFromC))
    {
    }

    static constexpr PitchClass fromSemitonesFromC (int semitonesFromC) noexcept
    {
        return PitchClass { semitonesFromC };
    }

    static constexpr PitchClass c() noexcept { return PitchClass { 0 }; }
    static constexpr PitchClass cSharp() noexcept { return PitchClass { 1 }; }
    static constexpr PitchClass d() noexcept { return PitchClass { 2 }; }
    static constexpr PitchClass dSharp() noexcept { return PitchClass { 3 }; }
    static constexpr PitchClass e() noexcept { return PitchClass { 4 }; }
    static constexpr PitchClass f() noexcept { return PitchClass { 5 }; }
    static constexpr PitchClass fSharp() noexcept { return PitchClass { 6 }; }
    static constexpr PitchClass g() noexcept { return PitchClass { 7 }; }
    static constexpr PitchClass gSharp() noexcept { return PitchClass { 8 }; }
    static constexpr PitchClass a() noexcept { return PitchClass { 9 }; }
    static constexpr PitchClass aSharp() noexcept { return PitchClass { 10 }; }
    static constexpr PitchClass b() noexcept { return PitchClass { 11 }; }

    constexpr int semitonesFromC() const noexcept
    {
        return semitonesFromC_;
    }

    constexpr PitchClass transposedBy (int semitones) const noexcept
    {
        return PitchClass { semitonesFromC_ + semitones };
    }

private:
    static constexpr int normalize (int semitones) noexcept
    {
        const auto result = semitones % 12;
        return result < 0 ? result + 12 : result;
    }

    int semitonesFromC_ = 0;
};

constexpr bool operator== (PitchClass lhs, PitchClass rhs) noexcept
{
    return lhs.semitonesFromC() == rhs.semitonesFromC();
}

constexpr bool operator!= (PitchClass lhs, PitchClass rhs) noexcept
{
    return ! (lhs == rhs);
}

bool hasNaturalLetterName (PitchClass pitchClass) noexcept;
}
