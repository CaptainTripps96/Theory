#pragma once

#include "core/music_theory/Chord.h"
#include "core/music_theory/MidiPitch.h"

#include <optional>
#include <vector>

namespace tsq::core::music_theory
{
class ChordRecognizer
{
public:
    std::optional<Chord> recognize (const std::vector<MidiPitch>& pitches) const;
    std::optional<Chord> recognize (const std::vector<PitchClass>& pitchClasses) const;
};
}
