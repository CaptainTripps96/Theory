#pragma once

#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/ChordRegion.h"
#include "core/sequencing/HarmonicContext.h"
#include "core/sequencing/MusicalStructure.h"

#include <string>
#include <vector>

namespace tsq::core::sequencing
{
struct CompatibleScaleSuggestion
{
    HarmonicContext context;
    std::string displayName;
};

bool isChordDiatonicInContext (const ChordRegion& chordRegion,
                               const HarmonicContext& context,
                               const music_theory::ScaleLibrary& scaleLibrary);

bool isBorrowedChordRegion (const MusicalStructure& structure,
                            const ChordRegion& chordRegion,
                            const music_theory::ScaleLibrary& scaleLibrary);

std::vector<CompatibleScaleSuggestion> compatibleScaleSuggestionsFor (const MusicalStructure& structure,
                                                                      const ChordRegion& chordRegion,
                                                                      const music_theory::ScaleLibrary& scaleLibrary);
}
