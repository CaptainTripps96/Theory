#include "core/time/ProjectRhythmSettings.h"

#include <stdexcept>
#include <utility>

namespace tsq::core::time
{
const std::string& ProjectRhythmSettings::currentGridDivisionId() const noexcept
{
    return currentGridDivisionId_;
}

void ProjectRhythmSettings::setCurrentGridDivisionId (std::string gridDivisionId)
{
    if (gridDivisionId.empty())
        throw std::invalid_argument ("Current grid division ID must not be empty");

    currentGridDivisionId_ = std::move (gridDivisionId);
}

bool ProjectRhythmSettings::tripletsEnabled() const noexcept
{
    return tripletsEnabled_;
}

bool ProjectRhythmSettings::quintupletsEnabled() const noexcept
{
    return quintupletsEnabled_;
}

bool ProjectRhythmSettings::septupletsEnabled() const noexcept
{
    return septupletsEnabled_;
}

bool ProjectRhythmSettings::nonupletsEnabled() const noexcept
{
    return nonupletsEnabled_;
}

void ProjectRhythmSettings::setTripletsEnabled (bool enabled) noexcept
{
    tripletsEnabled_ = enabled;
}

void ProjectRhythmSettings::setQuintupletsEnabled (bool enabled) noexcept
{
    quintupletsEnabled_ = enabled;
}

void ProjectRhythmSettings::setSeptupletsEnabled (bool enabled) noexcept
{
    septupletsEnabled_ = enabled;
}

void ProjectRhythmSettings::setNonupletsEnabled (bool enabled) noexcept
{
    nonupletsEnabled_ = enabled;
}
}
