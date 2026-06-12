#include "core/commands/ProjectCommandContext.h"

namespace tsq::core::commands
{
ProjectCommandContext::ProjectCommandContext (sequencing::Project& project) noexcept
    : project_ (project)
{
}

sequencing::Project& ProjectCommandContext::project() noexcept
{
    return project_;
}

const sequencing::Project& ProjectCommandContext::project() const noexcept
{
    return project_;
}
}
