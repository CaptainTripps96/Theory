#pragma once

#include "core/music_theory/MidiPitch.h"
#include "core/music_theory/NoteName.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/HarmonicContext.h"
#include "core/time/GridDivision.h"
#include "core/time/ProjectRhythmSettings.h"
#include "core/time/Tick.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tsq::core::sequencing
{
enum class ScaleLockMode
{
    off,
    nearest,
    roundUp,
    roundDown
};

std::string scaleLockModeName (ScaleLockMode mode);
std::vector<time::GridDivisionDefinition> availableInputQuantizationDivisions (const time::ProjectRhythmSettings& settings);
std::optional<time::GridDivisionDefinition> inputQuantizationDivisionById (std::string_view id,
                                                                           const time::ProjectRhythmSettings& settings);
time::TickPosition quantizeInputStart (time::TickPosition position,
                                       std::string_view gridDivisionId,
                                       const time::ProjectRhythmSettings& settings);
music_theory::MidiPitch applyScaleLock (music_theory::MidiPitch pitch,
                                        const HarmonicContext& context,
                                        const music_theory::ScaleLibrary& scaleLibrary,
                                        ScaleLockMode mode);
music_theory::NoteName spellingForRecordedPitch (music_theory::MidiPitch pitch,
                                                 const HarmonicContext& context,
                                                 const music_theory::ScaleLibrary& scaleLibrary);
}
