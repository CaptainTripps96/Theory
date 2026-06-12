#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/ScaleModeRegion.h"

namespace tsq::core::commands
{
class DeleteScaleModeRegionCommand final : public Command
{
public:
    explicit DeleteScaleModeRegionCommand (sequencing::ScaleModeRegion region);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ScaleModeRegion region_;
};
}
