#pragma once

#include "core/commands/Command.h"
#include "core/time/Tick.h"

#include <optional>

namespace tsq::core::commands
{
enum class TimelineClipKind
{
    midi,
    audio
};

class MoveClipCommand : public Command
{
public:
    MoveClipCommand (std::string trackId, std::string clipId, time::TickPosition newStartInProject);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    time::TickPosition newStartInProject_ {};
    std::optional<time::TickPosition> previousStartInProject_;
    std::optional<TimelineClipKind> clipKind_;
};
}
