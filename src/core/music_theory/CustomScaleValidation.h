#pragma once

#include "core/music_theory/PitchClass.h"
#include "core/music_theory/ScaleMetadata.h"

#include <string>
#include <vector>

namespace tsq::core::music_theory
{
struct CustomScaleSpecification
{
    ScaleMetadata metadata;
    std::vector<PitchClass> selectedPitchClassesFromC;
};

enum class CustomScaleValidationIssueCode
{
    emptyName,
    missingRootC,
    duplicatePitchClass
};

struct CustomScaleValidationIssue
{
    CustomScaleValidationIssueCode code;
    std::string message;
};

class CustomScaleValidationResult
{
public:
    explicit CustomScaleValidationResult (std::vector<CustomScaleValidationIssue> issues);

    bool isValid() const noexcept;
    const std::vector<CustomScaleValidationIssue>& issues() const noexcept;
    bool hasIssue (CustomScaleValidationIssueCode code) const noexcept;

private:
    std::vector<CustomScaleValidationIssue> issues_;
};

CustomScaleValidationResult validateCustomScaleSpecification (const CustomScaleSpecification& specification);
}
