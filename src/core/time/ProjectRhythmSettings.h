#pragma once

#include <string>

namespace tsq::core::time
{
class ProjectRhythmSettings
{
public:
    static constexpr const char* defaultGridDivisionId = "sixteenth";

    const std::string& currentGridDivisionId() const noexcept;
    void setCurrentGridDivisionId (std::string gridDivisionId);

    bool tripletsEnabled() const noexcept;
    bool quintupletsEnabled() const noexcept;
    bool septupletsEnabled() const noexcept;
    bool nonupletsEnabled() const noexcept;

    void setTripletsEnabled (bool enabled) noexcept;
    void setQuintupletsEnabled (bool enabled) noexcept;
    void setSeptupletsEnabled (bool enabled) noexcept;
    void setNonupletsEnabled (bool enabled) noexcept;

private:
    std::string currentGridDivisionId_ = defaultGridDivisionId;
    bool tripletsEnabled_ = true;
    bool quintupletsEnabled_ = false;
    bool septupletsEnabled_ = false;
    bool nonupletsEnabled_ = false;
};
}
