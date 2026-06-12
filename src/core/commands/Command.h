#pragma once

#include "core/diagnostics/Result.h"

#include <string>

namespace tsq::core::commands
{
class ProjectCommandContext;

using CommandResult = diagnostics::Result;

class Command
{
public:
    virtual ~Command() = default;

    virtual std::string name() const = 0;
    virtual CommandResult execute (ProjectCommandContext& context) = 0;
    virtual CommandResult undo (ProjectCommandContext& context) = 0;
};
}
