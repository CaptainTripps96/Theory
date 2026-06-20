#pragma once

#include "core/commands/Command.h"
#include "core/time/TempoMap.h"

#include <optional>

namespace tsq::core::commands
{
class AddTempoNodeCommand : public Command
{
public:
    AddTempoNodeCommand (time::TickPosition position, time::Tempo tempo);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::tempoMap; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    time::TickPosition position_ {};
    time::Tempo tempo_ {};
    std::optional<time::TempoMap> previousMap_;
};
}
