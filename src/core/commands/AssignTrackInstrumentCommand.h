#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/TrackInstrumentReference.h"

#include <optional>
#include <string>

namespace tsq::core::commands
{
class AssignTrackInstrumentCommand final : public Command
{
public:
    AssignTrackInstrumentCommand (std::string trackId, sequencing::TrackInstrumentReference instrument);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::deviceChain; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::TrackInstrumentReference instrument_;
    std::optional<sequencing::TrackInstrumentReference> previousInstrument_;
    bool hadPreviousInstrument_ = false;
};
}
