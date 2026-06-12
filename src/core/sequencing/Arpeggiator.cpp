#include "core/sequencing/Arpeggiator.h"

#include <algorithm>

namespace tsq::core::sequencing
{
namespace
{
constexpr ArpeggioPattern patternCycle[] {
    ArpeggioPattern::up,
    ArpeggioPattern::down,
    ArpeggioPattern::upDown,
    ArpeggioPattern::downUp,
    ArpeggioPattern::outsideIn,
    ArpeggioPattern::insideOut
};

std::size_t indexOf (ArpeggioPattern pattern) noexcept
{
    for (auto index = std::size_t {}; index < std::size (patternCycle); ++index)
    {
        if (patternCycle[index] == pattern)
            return index;
    }

    return 0;
}

std::size_t selectedSubdivisionIndex (std::string_view currentSubdivisionId,
                                      const std::vector<time::GridDivisionDefinition>& subdivisions)
{
    const auto match = std::find_if (subdivisions.begin(), subdivisions.end(), [currentSubdivisionId] (const auto& subdivision) {
        return subdivision.id == currentSubdivisionId;
    });

    if (match != subdivisions.end())
        return static_cast<std::size_t> (std::distance (subdivisions.begin(), match));

    const auto defaultMatch = std::find_if (subdivisions.begin(), subdivisions.end(), [] (const auto& subdivision) {
        return subdivision.id == time::ProjectRhythmSettings::defaultGridDivisionId;
    });

    return defaultMatch == subdivisions.end()
        ? 0
        : static_cast<std::size_t> (std::distance (subdivisions.begin(), defaultMatch));
}
}

std::string arpeggioPatternName (ArpeggioPattern pattern)
{
    switch (pattern)
    {
        case ArpeggioPattern::up: return "Up";
        case ArpeggioPattern::down: return "Down";
        case ArpeggioPattern::upDown: return "Up-Down";
        case ArpeggioPattern::downUp: return "Down-Up";
        case ArpeggioPattern::outsideIn: return "Outside-In";
        case ArpeggioPattern::insideOut: return "Inside-Out";
    }

    return "Up";
}

ArpeggioPattern nextArpeggioPattern (ArpeggioPattern pattern) noexcept
{
    const auto index = indexOf (pattern);
    return patternCycle[(index + 1) % std::size (patternCycle)];
}

ArpeggioPattern previousArpeggioPattern (ArpeggioPattern pattern) noexcept
{
    const auto index = indexOf (pattern);
    return patternCycle[(index + std::size (patternCycle) - 1) % std::size (patternCycle)];
}

std::vector<time::GridDivisionDefinition> availableArpeggioSubdivisions (const time::ProjectRhythmSettings& settings)
{
    auto result = time::availableGridDivisions (settings);
    std::stable_sort (result.begin(), result.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.tickDuration.ticks() > rhs.tickDuration.ticks();
    });
    return result;
}

std::string shorterArpeggioSubdivisionId (std::string_view currentSubdivisionId, const time::ProjectRhythmSettings& settings)
{
    const auto subdivisions = availableArpeggioSubdivisions (settings);
    if (subdivisions.empty())
        return time::ProjectRhythmSettings::defaultGridDivisionId;

    const auto index = selectedSubdivisionIndex (currentSubdivisionId, subdivisions);
    const auto nextIndex = std::min (index + 1, subdivisions.size() - 1);
    return subdivisions[nextIndex].id;
}

std::string longerArpeggioSubdivisionId (std::string_view currentSubdivisionId, const time::ProjectRhythmSettings& settings)
{
    const auto subdivisions = availableArpeggioSubdivisions (settings);
    if (subdivisions.empty())
        return time::ProjectRhythmSettings::defaultGridDivisionId;

    const auto index = selectedSubdivisionIndex (currentSubdivisionId, subdivisions);
    const auto nextIndex = index == 0 ? 0 : index - 1;
    return subdivisions[nextIndex].id;
}
}
