#include "core/sequencing/HarmonicOverlay.h"

#include "core/sequencing/HarmonicContext.h"

#include <algorithm>

namespace tsq::core::sequencing
{
namespace
{
const ChordRegion* activeChordAt (const MusicalStructure& structure, time::TickPosition position) noexcept
{
    const ChordRegion* result = nullptr;

    for (const auto& region : structure.chordRegions())
    {
        if (! region.region().contains (position))
            continue;

        if (result == nullptr || region.start() >= result->start())
            result = &region;
    }

    return result;
}

bool containsPitchClass (const std::vector<music_theory::PitchClass>& pitchClasses,
                         music_theory::PitchClass pitchClass)
{
    return std::find (pitchClasses.begin(), pitchClasses.end(), pitchClass) != pitchClasses.end();
}
}

HarmonicOverlayRole harmonicOverlayRoleAt (const MusicalStructure& structure,
                                           const music_theory::ScaleLibrary& scaleLibrary,
                                           time::TickPosition projectPosition,
                                           music_theory::PitchClass pitchClass)
{
    const auto* chord = activeChordAt (structure, projectPosition);
    if (chord == nullptr)
        return HarmonicOverlayRole::none;

    const HarmonicContext context {
        structure.keyCenterAt (projectPosition),
        structure.scaleDefinitionNameAt (projectPosition)
    };

    if (! context.contains (pitchClass, scaleLibrary))
        return HarmonicOverlayRole::accidental;

    if (pitchClass == chord->root())
        return HarmonicOverlayRole::root;

    if (containsPitchClass (chord->chordTones(), pitchClass))
        return HarmonicOverlayRole::chordTone;

    return HarmonicOverlayRole::nonChordScaleTone;
}
}
