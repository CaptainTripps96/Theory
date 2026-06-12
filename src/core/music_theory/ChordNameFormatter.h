#pragma once

#include "core/music_theory/Chord.h"
#include "core/music_theory/EnharmonicSpelling.h"

#include <string>

namespace tsq::core::music_theory
{
class ChordNameFormatter
{
public:
    std::string format (const Chord& chord,
                        SpellingPreference spellingPreference = SpellingPreference::preferSharps) const;
};
}
