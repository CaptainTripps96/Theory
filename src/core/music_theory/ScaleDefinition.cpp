#include "core/music_theory/ScaleDefinition.h"

#include <array>
#include <stdexcept>
#include <utility>

namespace tsq::core::music_theory
{
namespace
{
int normalizePitchClassOffset (int offset) noexcept
{
    const auto result = offset % 12;
    return result < 0 ? result + 12 : result;
}
}

ScaleDefinition::ScaleDefinition (ScaleMetadata metadata,
                                  std::vector<int> pitchClassOffsetsFromRoot,
                                  std::vector<ScaleDegree> degreeMapping,
                                  SpellingPreference preferredSpelling)
    : metadata_ (std::move (metadata)),
      pitchClassOffsetsFromRoot_ (std::move (pitchClassOffsetsFromRoot)),
      degreeMapping_ (std::move (degreeMapping)),
      preferredSpelling_ (preferredSpelling)
{
    if (metadata_.name.empty())
        throw std::invalid_argument ("ScaleDefinition requires a name");

    if (pitchClassOffsetsFromRoot_.empty())
        throw std::invalid_argument ("ScaleDefinition requires at least one pitch-class offset");

    if (pitchClassOffsetsFromRoot_.size() != degreeMapping_.size())
        throw std::invalid_argument ("ScaleDefinition pitch-class offsets and degree mapping must have the same size");

    std::array<bool, 12> seenOffsets {};
    for (auto& offset : pitchClassOffsetsFromRoot_)
    {
        offset = normalizePitchClassOffset (offset);
        if (seenOffsets[static_cast<std::size_t> (offset)])
            throw std::invalid_argument ("ScaleDefinition pitch-class offsets must be unique");

        seenOffsets[static_cast<std::size_t> (offset)] = true;
    }
}

const ScaleMetadata& ScaleDefinition::metadata() const noexcept
{
    return metadata_;
}

const std::string& ScaleDefinition::name() const noexcept
{
    return metadata_.name;
}

const std::string& ScaleDefinition::category() const noexcept
{
    return metadata_.category;
}

const std::vector<std::string>& ScaleDefinition::tags() const noexcept
{
    return metadata_.tags;
}

const std::string& ScaleDefinition::description() const noexcept
{
    return metadata_.description;
}

const std::vector<int>& ScaleDefinition::pitchClassOffsetsFromRoot() const noexcept
{
    return pitchClassOffsetsFromRoot_;
}

const std::vector<ScaleDegree>& ScaleDefinition::degreeMapping() const noexcept
{
    return degreeMapping_;
}

SpellingPreference ScaleDefinition::preferredSpelling() const noexcept
{
    return preferredSpelling_;
}

bool ScaleDefinition::containsOffsetFromRoot (int pitchClassOffsetFromRoot) const noexcept
{
    const auto normalizedOffset = normalizePitchClassOffset (pitchClassOffsetFromRoot);

    for (const auto offset : pitchClassOffsetsFromRoot_)
    {
        if (offset == normalizedOffset)
            return true;
    }

    return false;
}
}
