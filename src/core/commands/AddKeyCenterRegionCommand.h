#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/KeyCenterRegion.h"

namespace tsq::core::commands
{
class AddKeyCenterRegionCommand : public Command
{
public:
    explicit AddKeyCenterRegionCommand (sequencing::KeyCenterRegion region);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::KeyCenterRegion region_;
};
}
