#include "core/music_theory/ChordNameFormatter.h"

namespace tsq::core::music_theory
{
std::string ChordNameFormatter::format (const Chord& chord, SpellingPreference spellingPreference) const
{
    return spellPitchClass (chord.root(), spellingPreference).toString() + chordQualitySuffix (chord.quality());
}
}
