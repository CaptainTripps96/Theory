#pragma once

#include "core/music_theory/PitchClass.h"
#include "core/music_theory/ScaleInstance.h"
#include "core/music_theory/ScaleLibrary.h"

#include <string>

namespace tsq::core::sequencing
{
class HarmonicContext
{
public:
    HarmonicContext (music_theory::PitchClass keyCenter, std::string scaleDefinitionName);

    music_theory::PitchClass keyCenter() const noexcept;
    const std::string& scaleDefinitionName() const noexcept;
    music_theory::ScaleInstance scaleInstance (const music_theory::ScaleLibrary& scaleLibrary) const;
    bool contains (music_theory::PitchClass pitchClass, const music_theory::ScaleLibrary& scaleLibrary) const;

private:
    music_theory::PitchClass keyCenter_;
    std::string scaleDefinitionName_;
};

bool operator== (const HarmonicContext& lhs, const HarmonicContext& rhs) noexcept;
bool operator!= (const HarmonicContext& lhs, const HarmonicContext& rhs) noexcept;
}
