#pragma once

#include "core/commands/Command.h"
#include "core/time/Tick.h"

#include <optional>

namespace tsq::core::commands
{
class ResizeNoteCommand : public Command
{
public:
    ResizeNoteCommand (std::string trackId, std::string clipId, std::string noteId, time::TickDuration newDuration);
    ResizeNoteCommand (std::string trackId,
                       std::string clipId,
                       std::string noteId,
                       time::TickPosition newStartInClip,
                       time::TickDuration newDuration);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::noteData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    std::string noteId_;
    std::optional<time::TickPosition> newStartInClip_;
    time::TickDuration newDuration_ {};
    std::optional<time::TickPosition> previousStartInClip_;
    std::optional<time::TickDuration> previousDuration_;
};
}
