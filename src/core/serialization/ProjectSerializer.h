#pragma once

#include "core/serialization/JsonHelpers.h"
#include "core/sequencing/Project.h"

#include <string>
#include <string_view>
#include <vector>

namespace tsq::core::serialization
{
struct ProjectLoadResult
{
    sequencing::Project project;
    std::vector<std::string> warnings;
};

class ProjectSerializer
{
public:
    static JsonValue toJson (const sequencing::Project& project);
    static sequencing::Project fromJson (JsonValue projectJson);
    static ProjectLoadResult fromJsonWithWarnings (JsonValue projectJson);

    static std::string serialize (const sequencing::Project& project);
    static sequencing::Project deserialize (std::string_view jsonText);
    static ProjectLoadResult deserializeWithWarnings (std::string_view jsonText);
};
}
