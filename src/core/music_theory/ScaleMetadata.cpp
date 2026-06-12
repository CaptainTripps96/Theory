#include "core/music_theory/ScaleMetadata.h"

#include <algorithm>
#include <cctype>

namespace tsq::core::music_theory
{
namespace
{
std::string normalizedText (std::string_view text)
{
    std::string result { text };
    std::transform (result.begin(), result.end(), result.begin(), [] (unsigned char c) {
        return static_cast<char> (std::tolower (c));
    });
    return result;
}

bool containsText (std::string_view haystack, const std::string& needle)
{
    return normalizedText (haystack).find (needle) != std::string::npos;
}
}

bool matchesScaleMetadataText (const ScaleMetadata& metadata, std::string_view query)
{
    const auto normalizedQuery = normalizedText (query);
    if (normalizedQuery.empty())
        return true;

    if (containsText (metadata.name, normalizedQuery) || containsText (metadata.category, normalizedQuery))
        return true;

    return std::any_of (metadata.tags.begin(), metadata.tags.end(), [&normalizedQuery] (const auto& tag) {
        return containsText (tag, normalizedQuery);
    });
}
}
