#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/AudioClip.h"
#include "core/sequencing/MidiClip.h"

#include <variant>

namespace tsq::core::commands
{
class AddClipCommand : public Command
{
public:
    AddClipCommand (std::string trackId, sequencing::MidiClip clip);
    AddClipCommand (std::string trackId, sequencing::AudioClip clip);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::variant<sequencing::MidiClip, sequencing::AudioClip> clip_;
};
}
