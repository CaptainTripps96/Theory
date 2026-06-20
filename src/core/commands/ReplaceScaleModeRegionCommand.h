#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/ScaleModeRegion.h"

namespace tsq::core::commands
{
class ReplaceScaleModeRegionCommand : public Command
{
public:
    ReplaceScaleModeRegionCommand (sequencing::ScaleModeRegion previousRegion, sequencing::ScaleModeRegion newRegion);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::harmonicStructure; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ScaleModeRegion previousRegion_;
    sequencing::ScaleModeRegion newRegion_;
};
}
