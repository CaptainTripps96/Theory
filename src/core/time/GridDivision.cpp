#include "core/time/GridDivision.h"

#include <stdexcept>
#include <utility>

namespace tsq::core::time
{
namespace
{
GridDivisionDefinition standardDefinition (GridDivision division)
{
    return GridDivisionDefinition {
        identifier (division),
        displayName (division),
        duration (division),
        false,
        0
    };
}

GridDivisionDefinition tupletDefinition (std::string id,
                                         std::string displayName,
                                         TickDuration tickDuration,
                                         int tupletNotes)
{
    return GridDivisionDefinition {
        std::move (id),
        std::move (displayName),
        tickDuration,
        true,
        tupletNotes
    };
}
}

int denominator (GridDivision division) noexcept
{
    switch (division)
    {
        case GridDivision::whole: return 1;
        case GridDivision::half: return 2;
        case GridDivision::quarter: return 4;
        case GridDivision::eighth: return 8;
        case GridDivision::sixteenth: return 16;
        case GridDivision::thirtySecond: return 32;
        case GridDivision::sixtyFourth: return 64;
    }

    return 4;
}

TickDuration duration (GridDivision division) noexcept
{
    return TickDuration::fromTicks ((ticksPerQuarterNote * 4) / denominator (division));
}

std::string identifier (GridDivision division)
{
    switch (division)
    {
        case GridDivision::whole: return "whole";
        case GridDivision::half: return "half";
        case GridDivision::quarter: return "quarter";
        case GridDivision::eighth: return "eighth";
        case GridDivision::sixteenth: return "sixteenth";
        case GridDivision::thirtySecond: return "thirtySecond";
        case GridDivision::sixtyFourth: return "sixtyFourth";
    }

    return "quarter";
}

std::string displayName (GridDivision division)
{
    switch (division)
    {
        case GridDivision::whole: return "1/1";
        case GridDivision::half: return "1/2";
        case GridDivision::quarter: return "1/4";
        case GridDivision::eighth: return "1/8";
        case GridDivision::sixteenth: return "1/16";
        case GridDivision::thirtySecond: return "1/32";
        case GridDivision::sixtyFourth: return "1/64";
    }

    return "1/4";
}

std::optional<GridDivision> gridDivisionFromIdentifier (std::string_view id) noexcept
{
    if (id == "whole") return GridDivision::whole;
    if (id == "half") return GridDivision::half;
    if (id == "quarter") return GridDivision::quarter;
    if (id == "eighth") return GridDivision::eighth;
    if (id == "sixteenth") return GridDivision::sixteenth;
    if (id == "thirtySecond") return GridDivision::thirtySecond;
    if (id == "sixtyFourth") return GridDivision::sixtyFourth;

    return std::nullopt;
}

std::vector<GridDivisionDefinition> availableGridDivisions (const ProjectRhythmSettings& settings)
{
    std::vector<GridDivisionDefinition> result {
        standardDefinition (GridDivision::whole),
        standardDefinition (GridDivision::half),
        standardDefinition (GridDivision::quarter),
        standardDefinition (GridDivision::eighth),
        standardDefinition (GridDivision::sixteenth),
        standardDefinition (GridDivision::thirtySecond),
        standardDefinition (GridDivision::sixtyFourth)
    };

    if (settings.tripletsEnabled())
    {
        result.push_back (tupletDefinition ("quarterTriplet", "1/4T", TickDuration::fromTicks ((ticksPerQuarterNote * 2) / 3), 3));
        result.push_back (tupletDefinition ("eighthTriplet", "1/8T", TickDuration::fromTicks (ticksPerQuarterNote / 3), 3));
        result.push_back (tupletDefinition ("sixteenthTriplet", "1/16T", TickDuration::fromTicks (ticksPerQuarterNote / 6), 3));
    }

    if (settings.quintupletsEnabled())
        result.push_back (tupletDefinition ("sixteenthQuintuplet", "1/16 5:4", TickDuration::fromTicks (ticksPerQuarterNote / 5), 5));

    if (settings.septupletsEnabled())
        result.push_back (tupletDefinition ("sixteenthSeptuplet", "1/16 7:4", TickDuration::fromTicks (ticksPerQuarterNote / 7), 7));

    if (settings.nonupletsEnabled())
        result.push_back (tupletDefinition ("sixteenthNonuplet", "1/16 9:4", TickDuration::fromTicks (ticksPerQuarterNote / 9), 9));

    return result;
}

GridDivisionDefinition gridDivisionDefinitionFor (std::string_view id, const ProjectRhythmSettings& settings)
{
    for (const auto& definition : availableGridDivisions (settings))
    {
        if (definition.id == id)
            return definition;
    }

    return gridDivisionDefinitionFor (ProjectRhythmSettings::defaultGridDivisionId, ProjectRhythmSettings {});
}
}
