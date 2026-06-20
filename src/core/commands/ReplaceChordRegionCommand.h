#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/ChordRegion.h"

namespace tsq::core::commands
{
class ReplaceChordRegionCommand final : public Command
{
public:
    ReplaceChordRegionCommand (sequencing::ChordRegion previousRegion, sequencing::ChordRegion newRegion);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::harmonicStructure; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ChordRegion previousRegion_;
    sequencing::ChordRegion newRegion_;
};
}
