#pragma once

#include "core/serialization/ProjectSerializer.h"
#include "core/sequencing/Project.h"

#include <filesystem>

namespace tsq::core::serialization
{
class ProjectPackage
{
public:
    static void save (const sequencing::Project& project, const std::filesystem::path& packagePath);
    static sequencing::Project load (const std::filesystem::path& packagePath);
    static ProjectLoadResult loadWithWarnings (const std::filesystem::path& packagePath);

    static std::filesystem::path projectJsonPath (const std::filesystem::path& packagePath);
};
}
