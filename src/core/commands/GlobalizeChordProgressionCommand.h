#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/ChordRegion.h"

#include <string>
#include <vector>

namespace tsq::core::commands
{
class GlobalizeChordProgressionCommand final : public Command
{
public:
    GlobalizeChordProgressionCommand (std::string trackId,
                                      std::string clipId,
                                      std::vector<std::string> selectedNoteIds);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

    const std::vector<sequencing::ChordRegion>& addedRegions() const noexcept;

private:
    std::string trackId_;
    std::string clipId_;
    std::vector<std::string> selectedNoteIds_;
    std::vector<sequencing::ChordRegion> addedRegions_;
};
}
