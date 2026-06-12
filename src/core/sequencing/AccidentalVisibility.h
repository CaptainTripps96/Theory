#pragma once

#include "core/music_theory/NoteName.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/MidiClip.h"

#include <vector>

namespace tsq::core::sequencing
{
enum class PitchLaneStatus
{
    nativeScale,
    usedAccidental,
    chromaticReveal
};

struct PitchLaneVisibility
{
    music_theory::PitchClass pitchClass;
    music_theory::NoteName spelling;
    PitchLaneStatus status;
    bool editable = true;
    bool greyed = false;
};

std::vector<PitchLaneVisibility> nativeScaleLanes (const ClipHarmonicMap& harmonicMap,
                                                   Region clipLocalRange,
                                                   const music_theory::ScaleLibrary& scaleLibrary);

std::vector<PitchLaneVisibility> usedAccidentalLanes (const MidiClip& clip,
                                                      Region clipLocalRange,
                                                      const music_theory::ScaleLibrary& scaleLibrary);

std::vector<PitchLaneVisibility> chromaticRevealLanes (const MidiClip& clip,
                                                       Region clipLocalRange,
                                                       const music_theory::ScaleLibrary& scaleLibrary);

std::vector<PitchLaneVisibility> visibleLanesForClip (const MidiClip& clip,
                                                      Region clipLocalRange,
                                                      const music_theory::ScaleLibrary& scaleLibrary,
                                                      bool chromaticReveal);
}
