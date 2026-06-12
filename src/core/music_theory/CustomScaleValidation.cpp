#include "core/music_theory/CustomScaleValidation.h"

#include <array>

namespace tsq::core::music_theory
{
namespace
{
CustomScaleValidationIssue issue (CustomScaleValidationIssueCode code, std::string message)
{
    return CustomScaleValidationIssue { code, std::move (message) };
}
}

CustomScaleValidationResult::CustomScaleValidationResult (std::vector<CustomScaleValidationIssue> issues)
    : issues_ (std::move (issues))
{
}

bool CustomScaleValidationResult::isValid() const noexcept
{
    return issues_.empty();
}

const std::vector<CustomScaleValidationIssue>& CustomScaleValidationResult::issues() const noexcept
{
    return issues_;
}

bool CustomScaleValidationResult::hasIssue (CustomScaleValidationIssueCode code) const noexcept
{
    for (const auto& validationIssue : issues_)
    {
        if (validationIssue.code == code)
            return true;
    }

    return false;
}

CustomScaleValidationResult validateCustomScaleSpecification (const CustomScaleSpecification& specification)
{
    std::vector<CustomScaleValidationIssue> issues;

    if (specification.metadata.name.empty())
        issues.push_back (issue (CustomScaleValidationIssueCode::emptyName, "Custom scale name must not be empty"));

    std::array<int, 12> counts {};
    for (const auto pitchClass : specification.selectedPitchClassesFromC)
        ++counts[static_cast<std::size_t> (pitchClass.semitonesFromC())];

    if (counts[0] == 0)
        issues.push_back (issue (CustomScaleValidationIssueCode::missingRootC, "Custom scale definitions must include C as the root pitch class"));

    for (const auto count : counts)
    {
        if (count > 1)
        {
            issues.push_back (issue (CustomScaleValidationIssueCode::duplicatePitchClass, "Custom scale definitions must not contain duplicate pitch classes"));
            break;
        }
    }

    return CustomScaleValidationResult { std::move (issues) };
}
}
