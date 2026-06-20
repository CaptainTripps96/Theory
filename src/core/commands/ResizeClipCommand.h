#pragma once

#include "core/commands/Command.h"
#include "core/commands/MoveClipCommand.h"
#include "core/sequencing/ClipLoop.h"
#include "core/time/Tick.h"

#include <optional>

namespace tsq::core::commands
{
class ResizeClipCommand : public Command
{
public:
    ResizeClipCommand (std::string trackId, std::string clipId, time::TickDuration newLength);
    ResizeClipCommand (std::string trackId, std::string clipId, time::TickDuration newLength, sequencing::ClipLoop newLoop);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::clipData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    time::TickDuration newLength_ {};
    std::optional<sequencing::ClipLoop> newLoop_;
    std::optional<time::TickDuration> previousLength_;
    std::optional<sequencing::ClipLoop> previousLoop_;
    std::optional<TimelineClipKind> clipKind_;
};
}
