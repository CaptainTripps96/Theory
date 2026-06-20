#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/MidiClip.h"
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
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::clipData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    time::TickPosition newStartInProject_ {};
    std::optional<time::TickPosition> previousStartInProject_;
    std::optional<TimelineClipKind> clipKind_;
};

class MoveMidiClipToTrackCommand final : public Command
{
public:
    MoveMidiClipToTrackCommand (std::string sourceTrackId,
                                std::string destinationTrackId,
                                std::string clipId,
                                time::TickPosition newStartInProject);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::clipData; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string sourceTrackId_;
    std::string destinationTrackId_;
    std::string clipId_;
    time::TickPosition newStartInProject_ {};
    std::optional<sequencing::MidiClip> previousClip_;
};
}
