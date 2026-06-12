#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/ChordRegion.h"

namespace tsq::core::commands
{
class DeleteChordRegionCommand final : public Command
{
public:
    explicit DeleteChordRegionCommand (sequencing::ChordRegion region);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ChordRegion region_;
};
}
