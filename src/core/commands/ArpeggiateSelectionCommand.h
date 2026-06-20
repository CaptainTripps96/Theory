#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/Arpeggiator.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/MidiNote.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::commands
{
class ArpeggiateSelectionCommand final : public Command
{
public:
    ArpeggiateSelectionCommand (std::string trackId,
                                std::string clipId,
                                std::vector<std::string> selectedNoteIds,
                                std::string subdivisionId,
                                sequencing::ArpeggioPattern pattern);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::noteData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

    const std::vector<std::string>& resultingSelectionNoteIds() const noexcept;

private:
    std::string trackId_;
    std::string clipId_;
    std::vector<std::string> selectedNoteIds_;
    std::string subdivisionId_;
    sequencing::ArpeggioPattern pattern_ = sequencing::ArpeggioPattern::up;
    std::vector<sequencing::MidiNote> previousNotes_;
    std::vector<sequencing::MidiNote> replacementNotes_;
    std::vector<std::string> resultingSelectionNoteIds_;
    std::optional<sequencing::ExpressionState> previousExpressionState_;
};
}
