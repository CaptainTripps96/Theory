#pragma once

#include "core/commands/Command.h"
#include "core/music_theory/MidiPitch.h"
#include "core/music_theory/NoteName.h"
#include "core/sequencing/NoteHarmonicInterpretation.h"
#include "core/time/Tick.h"

#include <optional>

namespace tsq::core::commands
{
class MoveNoteCommand : public Command
{
public:
    MoveNoteCommand (std::string trackId, std::string clipId, std::string noteId, time::TickPosition newStartInClip);
    MoveNoteCommand (std::string trackId,
                     std::string clipId,
                     std::string noteId,
                     time::TickPosition newStartInClip,
                     music_theory::MidiPitch newPitch,
                     std::optional<music_theory::NoteName> newSpelling = std::nullopt);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::noteData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    std::string noteId_;
    time::TickPosition newStartInClip_ {};
    std::optional<music_theory::MidiPitch> newPitch_;
    std::optional<music_theory::NoteName> newSpelling_;
    std::optional<time::TickPosition> previousStartInClip_;
    std::optional<music_theory::MidiPitch> previousPitch_;
    std::optional<music_theory::NoteName> previousSpelling_;
    std::optional<sequencing::NoteHarmonicInterpretation> previousHarmonicInterpretation_;
};
}
