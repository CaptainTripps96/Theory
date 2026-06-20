#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/ScaleModeRegion.h"

namespace tsq::core::commands
{
class AddScaleModeRegionCommand : public Command
{
public:
    explicit AddScaleModeRegionCommand (sequencing::ScaleModeRegion region);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::harmonicStructure; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ScaleModeRegion region_;
};
}
