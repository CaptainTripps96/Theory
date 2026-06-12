#pragma once

#include "core/time/GridDivision.h"
#include "core/time/ProjectRhythmSettings.h"

#include <string>
#include <string_view>
#include <vector>

namespace tsq::core::sequencing
{
enum class ArpeggioPattern
{
    up,
    down,
    upDown,
    downUp,
    outsideIn,
    insideOut
};

std::string arpeggioPatternName (ArpeggioPattern pattern);
ArpeggioPattern nextArpeggioPattern (ArpeggioPattern pattern) noexcept;
ArpeggioPattern previousArpeggioPattern (ArpeggioPattern pattern) noexcept;

std::vector<time::GridDivisionDefinition> availableArpeggioSubdivisions (const time::ProjectRhythmSettings& settings);
std::string shorterArpeggioSubdivisionId (std::string_view currentSubdivisionId, const time::ProjectRhythmSettings& settings);
std::string longerArpeggioSubdivisionId (std::string_view currentSubdivisionId, const time::ProjectRhythmSettings& settings);
}
