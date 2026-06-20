#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/MidiClip.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::commands
{
class SeparateNotesToClipCommand final : public Command
{
public:
    SeparateNotesToClipCommand (std::string trackId,
                                std::string sourceClipId,
                                std::string separatedClipId,
                                std::vector<std::string> selectedNoteIds);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::clipData | PlaybackSyncCategory::noteData | PlaybackSyncCategory::expression; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

    const std::string& separatedClipId() const noexcept { return separatedClipId_; }

private:
    std::string trackId_;
    std::string sourceClipId_;
    std::string separatedClipId_;
    std::vector<std::string> selectedNoteIds_;
    std::optional<sequencing::MidiClip> originalSourceClip_;
    std::optional<sequencing::MidiClip> separatedClip_;
    std::optional<sequencing::MidiClip> sourceClipAfterSeparation_;
};
}
