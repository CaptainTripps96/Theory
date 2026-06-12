#pragma once

#include "core/music_theory/EnharmonicSpelling.h"
#include "core/music_theory/ScaleDegree.h"
#include "core/music_theory/ScaleMetadata.h"

#include <string>
#include <vector>

namespace tsq::core::music_theory
{
class ScaleDefinition
{
public:
    ScaleDefinition (ScaleMetadata metadata,
                     std::vector<int> pitchClassOffsetsFromRoot,
                     std::vector<ScaleDegree> degreeMapping,
                     SpellingPreference preferredSpelling = SpellingPreference::preferSharps);

    const ScaleMetadata& metadata() const noexcept;
    const std::string& name() const noexcept;
    const std::string& category() const noexcept;
    const std::vector<std::string>& tags() const noexcept;
    const std::string& description() const noexcept;
    const std::vector<int>& pitchClassOffsetsFromRoot() const noexcept;
    const std::vector<ScaleDegree>& degreeMapping() const noexcept;
    SpellingPreference preferredSpelling() const noexcept;

    bool containsOffsetFromRoot (int pitchClassOffsetFromRoot) const noexcept;

private:
    ScaleMetadata metadata_;
    std::vector<int> pitchClassOffsetsFromRoot_;
    std::vector<ScaleDegree> degreeMapping_;
    SpellingPreference preferredSpelling_ = SpellingPreference::preferSharps;
};
}
