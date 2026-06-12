#include "core/music_theory/CustomScaleBuilder.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace tsq::core::music_theory
{
namespace
{
ScaleDegree degreeForCKeyboardPitchClass (int semitonesFromC)
{
    switch (semitonesFromC)
    {
        case 0: return ScaleDegree::natural (1);
        case 1: return ScaleDegree::sharp (1);
        case 2: return ScaleDegree::natural (2);
        case 3: return ScaleDegree::sharp (2);
        case 4: return ScaleDegree::natural (3);
        case 5: return ScaleDegree::natural (4);
        case 6: return ScaleDegree::sharp (4);
        case 7: return ScaleDegree::natural (5);
        case 8: return ScaleDegree::sharp (5);
        case 9: return ScaleDegree::natural (6);
        case 10: return ScaleDegree::sharp (6);
        case 11: return ScaleDegree::natural (7);
    }

    return ScaleDegree::natural (1);
}

std::string validationMessage (const CustomScaleValidationResult& validation)
{
    std::string result = "Invalid custom scale specification";

    for (const auto& issue : validation.issues())
    {
        result += ": ";
        result += issue.message;
    }

    return result;
}
}

ScaleDefinition CustomScaleBuilder::build (const CustomScaleSpecification& specification)
{
    const auto validation = validateCustomScaleSpecification (specification);
    if (! validation.isValid())
        throw std::invalid_argument (validationMessage (validation));

    std::array<bool, 12> selected {};
    for (const auto pitchClass : specification.selectedPitchClassesFromC)
        selected[static_cast<std::size_t> (pitchClass.semitonesFromC())] = true;

    std::vector<int> offsets;
    std::vector<ScaleDegree> degreeMapping;

    for (auto semitone = 0; semitone < 12; ++semitone)
    {
        if (! selected[static_cast<std::size_t> (semitone)])
            continue;

        offsets.push_back (semitone);
        degreeMapping.push_back (degreeForCKeyboardPitchClass (semitone));
    }

    return ScaleDefinition {
        specification.metadata,
        std::move (offsets),
        std::move (degreeMapping),
        SpellingPreference::preferSharps
    };
}

ScaleDefinition CustomScaleBuilder::build (ScaleMetadata metadata, std::vector<PitchClass> selectedPitchClassesFromC)
{
    return build (CustomScaleSpecification { std::move (metadata), std::move (selectedPitchClassesFromC) });
}
}
