#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/AudioClip.h"
#include "core/sequencing/MidiClip.h"

#include <optional>
#include <variant>

namespace tsq::core::commands
{
class DeleteClipCommand : public Command
{
public:
    DeleteClipCommand (std::string trackId, std::string clipId);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    std::optional<std::variant<sequencing::MidiClip, sequencing::AudioClip>> deletedClip_;
};
}
