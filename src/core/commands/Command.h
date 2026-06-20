#pragma once

#include "core/diagnostics/Result.h"

#include <cstdint>
#include <string>

namespace tsq::core::commands
{
class ProjectCommandContext;

using CommandResult = diagnostics::Result;

enum class PlaybackSyncCategory : std::uint32_t
{
    none = 0,
    unknown = 1u << 0,
    trackStructure = 1u << 1,
    clipData = 1u << 2,
    noteData = 1u << 3,
    automation = 1u << 4,
    mixer = 1u << 5,
    routing = 1u << 6,
    deviceChain = 1u << 7,
    tempoMap = 1u << 8,
    timeSignatureMap = 1u << 9,
    harmonicStructure = 1u << 10,
    editorRhythm = 1u << 11,
    customScales = 1u << 12,
    expression = 1u << 13
};

constexpr PlaybackSyncCategory operator| (PlaybackSyncCategory lhs, PlaybackSyncCategory rhs) noexcept
{
    return static_cast<PlaybackSyncCategory> (
        static_cast<std::uint32_t> (lhs) | static_cast<std::uint32_t> (rhs));
}

constexpr PlaybackSyncCategory operator& (PlaybackSyncCategory lhs, PlaybackSyncCategory rhs) noexcept
{
    return static_cast<PlaybackSyncCategory> (
        static_cast<std::uint32_t> (lhs) & static_cast<std::uint32_t> (rhs));
}

constexpr PlaybackSyncCategory& operator|= (PlaybackSyncCategory& lhs, PlaybackSyncCategory rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool anyPlaybackSyncCategory (PlaybackSyncCategory category) noexcept
{
    return static_cast<std::uint32_t> (category) != 0u;
}

constexpr bool playbackSyncRequired (PlaybackSyncCategory category) noexcept
{
    constexpr auto playbackRelevant = PlaybackSyncCategory::unknown
        | PlaybackSyncCategory::trackStructure
        | PlaybackSyncCategory::clipData
        | PlaybackSyncCategory::noteData
        | PlaybackSyncCategory::automation
        | PlaybackSyncCategory::mixer
        | PlaybackSyncCategory::routing
        | PlaybackSyncCategory::deviceChain
        | PlaybackSyncCategory::tempoMap
        | PlaybackSyncCategory::timeSignatureMap
        | PlaybackSyncCategory::expression;

    return anyPlaybackSyncCategory (category & playbackRelevant);
}

constexpr const char* playbackSyncCategoryLabel (PlaybackSyncCategory category) noexcept
{
    switch (category)
    {
        case PlaybackSyncCategory::none: return "none";
        case PlaybackSyncCategory::unknown: return "unknown";
        case PlaybackSyncCategory::trackStructure: return "track-structure";
        case PlaybackSyncCategory::clipData: return "clip-data";
        case PlaybackSyncCategory::noteData: return "note-data";
        case PlaybackSyncCategory::automation: return "automation";
        case PlaybackSyncCategory::mixer: return "mixer";
        case PlaybackSyncCategory::routing: return "routing";
        case PlaybackSyncCategory::deviceChain: return "device-chain";
        case PlaybackSyncCategory::tempoMap: return "tempo-map";
        case PlaybackSyncCategory::timeSignatureMap: return "time-signature-map";
        case PlaybackSyncCategory::harmonicStructure: return "harmonic-structure";
        case PlaybackSyncCategory::editorRhythm: return "editor-rhythm";
        case PlaybackSyncCategory::customScales: return "custom-scales";
        case PlaybackSyncCategory::expression: return "expression";
    }

    return "multiple";
}

class Command
{
public:
    virtual ~Command() = default;

    virtual std::string name() const = 0;
    virtual PlaybackSyncCategory playbackSyncCategory() const noexcept { return PlaybackSyncCategory::unknown; }
    virtual CommandResult execute (ProjectCommandContext& context) = 0;
    virtual CommandResult undo (ProjectCommandContext& context) = 0;
};
}
