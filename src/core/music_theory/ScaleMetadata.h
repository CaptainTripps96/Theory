#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tsq::core::music_theory
{
struct ScaleMetadata
{
    std::string name;
    std::string category;
    std::vector<std::string> tags;
    std::string description;
};

bool matchesScaleMetadataText (const ScaleMetadata& metadata, std::string_view query);
}
