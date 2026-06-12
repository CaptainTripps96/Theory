#pragma once

#include "core/music_theory/EnharmonicSpelling.h"

#include <string>

namespace tsq::core::music_theory
{
class MidiPitch
{
public:
    explicit MidiPitch (int value);

    static MidiPitch fromValue (int value);
    static MidiPitch middleC();

    int value() const noexcept;
    int octave() const noexcept;
    PitchClass pitchClass() const noexcept;
    MidiPitch transposedBy (int semitones) const;

    std::string nameWithOctave (SpellingPreference preference = SpellingPreference::preferSharps) const;
    std::string nameWithOctave (NoteName spelling) const;

private:
    int value_ = 60;
};

bool operator== (MidiPitch lhs, MidiPitch rhs) noexcept;
bool operator!= (MidiPitch lhs, MidiPitch rhs) noexcept;
}
