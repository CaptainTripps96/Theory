#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/MidiNote.h"

namespace tsq::core::commands
{
class AddNoteCommand : public Command
{
public:
    AddNoteCommand (std::string trackId, std::string clipId, sequencing::MidiNote note);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::noteData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    sequencing::MidiNote note_;
};
}
