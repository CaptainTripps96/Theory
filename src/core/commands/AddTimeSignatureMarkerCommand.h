#pragma once

#include "core/commands/Command.h"
#include "core/time/TimeSignatureMap.h"

#include <optional>

namespace tsq::core::commands
{
class AddTimeSignatureMarkerCommand : public Command
{
public:
    AddTimeSignatureMarkerCommand (time::TickPosition position, time::TimeSignature timeSignature);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    time::TickPosition position_ {};
    time::TimeSignature timeSignature_ {};
    std::optional<time::TimeSignatureMap> previousMap_;
};
}
