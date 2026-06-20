#pragma once

#include "core/commands/Command.h"
#include "core/time/ProjectRhythmSettings.h"

#include <optional>

namespace tsq::core::commands
{
class SetProjectRhythmSettingsCommand final : public Command
{
public:
    explicit SetProjectRhythmSettingsCommand (time::ProjectRhythmSettings newSettings);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::editorRhythm; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    time::ProjectRhythmSettings newSettings_;
    std::optional<time::ProjectRhythmSettings> previousSettings_;
};
}
