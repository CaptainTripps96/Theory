#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/MidiNote.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::commands
{
class StackDiatonicThirdCommand final : public Command
{
public:
    StackDiatonicThirdCommand (std::string trackId, std::string clipId, std::vector<std::string> selectedNoteIds);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::noteData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

    const std::vector<std::string>& resultingSelectionNoteIds() const noexcept;
    const std::vector<std::string>& addedNoteIds() const noexcept;

private:
    std::string trackId_;
    std::string clipId_;
    std::vector<std::string> selectedNoteIds_;
    std::vector<sequencing::MidiNote> addedNotes_;
    std::vector<std::string> resultingSelectionNoteIds_;
    std::vector<std::string> addedNoteIds_;
};

class RemoveHighestChordToneCommand final : public Command
{
public:
    RemoveHighestChordToneCommand (std::string trackId, std::string clipId, std::vector<std::string> selectedNoteIds);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::noteData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

    const std::vector<std::string>& resultingSelectionNoteIds() const noexcept;
    const std::vector<std::string>& removedNoteIds() const noexcept;

private:
    std::string trackId_;
    std::string clipId_;
    std::vector<std::string> selectedNoteIds_;
    std::vector<sequencing::MidiNote> removedNotes_;
    std::vector<std::string> resultingSelectionNoteIds_;
    std::vector<std::string> removedNoteIds_;
    std::optional<sequencing::ExpressionState> previousExpressionState_;
};

class InvertChordCommand final : public Command
{
public:
    enum class Direction
    {
        upward,
        downward
    };

    InvertChordCommand (std::string trackId,
                        std::string clipId,
                        std::vector<std::string> selectedNoteIds,
                        Direction direction);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::noteData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    std::vector<std::string> selectedNoteIds_;
    Direction direction_ = Direction::upward;
    std::vector<sequencing::MidiNote> previousNotes_;
    std::vector<sequencing::MidiNote> replacementNotes_;
};
}
