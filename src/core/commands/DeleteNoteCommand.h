#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/MidiNote.h"

#include <optional>

namespace tsq::core::commands
{
class DeleteNoteCommand : public Command
{
public:
    DeleteNoteCommand (std::string trackId, std::string clipId, std::string noteId);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::noteData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    std::string noteId_;
    std::optional<sequencing::MidiNote> deletedNote_;
    std::optional<sequencing::ExpressionState> previousExpressionState_;
};
}
