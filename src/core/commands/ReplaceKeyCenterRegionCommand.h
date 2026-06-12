#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/KeyCenterRegion.h"

namespace tsq::core::commands
{
class ReplaceKeyCenterRegionCommand : public Command
{
public:
    ReplaceKeyCenterRegionCommand (sequencing::KeyCenterRegion previousRegion, sequencing::KeyCenterRegion newRegion);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::KeyCenterRegion previousRegion_;
    sequencing::KeyCenterRegion newRegion_;
};
}
