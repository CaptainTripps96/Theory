#pragma once

#include "core/music_theory/NoteName.h"

#include <vector>

namespace tsq::core::music_theory
{
enum class SpellingPreference
{
    preferSharps,
    preferFlats
};

NoteName spellPitchClass (PitchClass pitchClass, SpellingPreference preference = SpellingPreference::preferSharps);
std::vector<NoteName> commonSpellingsFor (PitchClass pitchClass);
}
