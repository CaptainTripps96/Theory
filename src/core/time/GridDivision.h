#pragma once

#include "core/time/ProjectRhythmSettings.h"
#include "core/time/Tick.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tsq::core::time
{
enum class GridDivision
{
    whole,
    half,
    quarter,
    eighth,
    sixteenth,
    thirtySecond,
    sixtyFourth
};

struct GridDivisionDefinition
{
    std::string id;
    std::string displayName;
    TickDuration tickDuration {};
    bool tuplet = false;
    int tupletNotes = 0;
};

int denominator (GridDivision division) noexcept;
TickDuration duration (GridDivision division) noexcept;
std::string identifier (GridDivision division);
std::string displayName (GridDivision division);
std::optional<GridDivision> gridDivisionFromIdentifier (std::string_view id) noexcept;
std::vector<GridDivisionDefinition> availableGridDivisions (const ProjectRhythmSettings& settings);
GridDivisionDefinition gridDivisionDefinitionFor (std::string_view id, const ProjectRhythmSettings& settings);
}
