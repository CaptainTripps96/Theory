#pragma once

#include "core/music_theory/CustomScaleValidation.h"
#include "core/music_theory/ScaleDefinition.h"

namespace tsq::core::music_theory
{
class CustomScaleBuilder
{
public:
    static ScaleDefinition build (const CustomScaleSpecification& specification);
    static ScaleDefinition build (ScaleMetadata metadata, std::vector<PitchClass> selectedPitchClassesFromC);
};
}
