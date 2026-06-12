#pragma once

#include "core/commands/Command.h"
#include "core/music_theory/ScaleDefinition.h"

namespace tsq::core::commands
{
class AddCustomScaleCommand : public Command
{
public:
    explicit AddCustomScaleCommand (music_theory::ScaleDefinition scale);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    music_theory::ScaleDefinition scale_;
};
}
