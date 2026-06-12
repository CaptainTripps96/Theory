#include "core/sequencing/BorrowedChordAnalysis.h"

#include "core/music_theory/EnharmonicSpelling.h"

namespace tsq::core::sequencing
{
namespace
{
bool isUsefulSuggestionScale (const music_theory::ScaleDefinition& definition)
{
    return definition.pitchClassOffsetsFromRoot().size() < 12;
}
}

bool isChordDiatonicInContext (const ChordRegion& chordRegion,
                               const HarmonicContext& context,
                               const music_theory::ScaleLibrary& scaleLibrary)
{
    for (const auto chordTone : chordRegion.chordTones())
    {
        if (! context.contains (chordTone, scaleLibrary))
            return false;
    }

    return true;
}

bool isBorrowedChordRegion (const MusicalStructure& structure,
                            const ChordRegion& chordRegion,
                            const music_theory::ScaleLibrary& scaleLibrary)
{
    const HarmonicContext context {
        structure.keyCenterAt (chordRegion.start()),
        structure.scaleDefinitionNameAt (chordRegion.start())
    };

    return ! isChordDiatonicInContext (chordRegion, context, scaleLibrary);
}

std::vector<CompatibleScaleSuggestion> compatibleScaleSuggestionsFor (const MusicalStructure& structure,
                                                                      const ChordRegion& chordRegion,
                                                                      const music_theory::ScaleLibrary& scaleLibrary)
{
    const auto keyCenter = structure.keyCenterAt (chordRegion.start());
    const auto activeScale = structure.scaleDefinitionNameAt (chordRegion.start());

    std::vector<CompatibleScaleSuggestion> result;
    for (const auto& definition : scaleLibrary.definitions())
    {
        if (definition.name() == activeScale || ! isUsefulSuggestionScale (definition))
            continue;

        HarmonicContext candidate { keyCenter, definition.name() };
        if (isChordDiatonicInContext (chordRegion, candidate, scaleLibrary))
        {
            result.push_back (CompatibleScaleSuggestion {
                candidate,
                music_theory::spellPitchClass (keyCenter).toString() + " " + definition.name()
            });
        }
    }

    return result;
}
}
