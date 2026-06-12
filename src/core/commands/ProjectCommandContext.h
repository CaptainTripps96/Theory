#pragma once

#include "core/sequencing/Project.h"

namespace tsq::core::commands
{
class ProjectCommandContext
{
public:
    explicit ProjectCommandContext (sequencing::Project& project) noexcept;

    sequencing::Project& project() noexcept;
    const sequencing::Project& project() const noexcept;

private:
    sequencing::Project& project_;
};
}
