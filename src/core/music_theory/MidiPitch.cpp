#include "core/music_theory/MidiPitch.h"

#include <stdexcept>

namespace tsq::core::music_theory
{
MidiPitch::MidiPitch (int value)
    : value_ (value)
{
    if (value_ < 0 || value_ > 127)
        throw std::invalid_argument ("MIDI pitch must be between 0 and 127");
}

MidiPitch MidiPitch::fromValue (int value)
{
    return MidiPitch { value };
}

MidiPitch MidiPitch::middleC()
{
    return MidiPitch { 60 };
}

int MidiPitch::value() const noexcept
{
    return value_;
}

int MidiPitch::octave() const noexcept
{
    return (value_ / 12) - 1;
}

PitchClass MidiPitch::pitchClass() const noexcept
{
    return PitchClass::fromSemitonesFromC (value_);
}

MidiPitch MidiPitch::transposedBy (int semitones) const
{
    return MidiPitch { value_ + semitones };
}

std::string MidiPitch::nameWithOctave (SpellingPreference preference) const
{
    return nameWithOctave (spellPitchClass (pitchClass(), preference));
}

std::string MidiPitch::nameWithOctave (NoteName spelling) const
{
    return spelling.toString() + std::to_string (octave());
}

bool operator== (MidiPitch lhs, MidiPitch rhs) noexcept
{
    return lhs.value() == rhs.value();
}

bool operator!= (MidiPitch lhs, MidiPitch rhs) noexcept
{
    return ! (lhs == rhs);
}
}
