#pragma once

#include "core/music_theory/NoteName.h"
#include "core/music_theory/ScaleDefinition.h"

#include <vector>

namespace tsq::core::music_theory
{
class ScaleInstance
{
public:
    ScaleInstance (NoteName root, ScaleDefinition definition);

    const NoteName& root() const noexcept;
    const ScaleDefinition& definition() const noexcept;

    std::vector<PitchClass> pitchClasses() const;
    bool contains (PitchClass pitchClass) const noexcept;
    std::vector<NoteName> visibleNoteSpellings() const;

private:
    NoteName root_;
    ScaleDefinition definition_;
};
}
