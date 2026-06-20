#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/Track.h"

namespace tsq::core::commands
{
class AddTrackCommand : public Command
{
public:
    explicit AddTrackCommand (sequencing::Track track);
    AddTrackCommand (std::string trackId, std::string trackName, sequencing::TrackType trackType);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::trackStructure; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::Track track_;
};
}
