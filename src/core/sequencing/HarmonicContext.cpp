#include "core/sequencing/HarmonicContext.h"

#include "core/music_theory/EnharmonicSpelling.h"

#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
music_theory::NoteName spellKeyCenter (music_theory::PitchClass pitchClass)
{
    using music_theory::NoteName;

    switch (pitchClass.semitonesFromC())
    {
        case 0: return NoteName::c();
        case 1: return NoteName::cSharp();
        case 2: return NoteName::d();
        case 3: return NoteName::eFlat();
        case 4: return NoteName::e();
        case 5: return NoteName::f();
        case 6: return NoteName::fSharp();
        case 7: return NoteName::g();
        case 8: return NoteName::aFlat();
        case 9: return NoteName::a();
        case 10: return NoteName::bFlat();
        case 11: return NoteName::b();
    }

    return NoteName::c();
}
}

HarmonicContext::HarmonicContext (music_theory::PitchClass keyCenter, std::string scaleDefinitionName)
    : keyCenter_ (keyCenter),
      scaleDefinitionName_ (std::move (scaleDefinitionName))
{
    if (scaleDefinitionName_.empty())
        throw std::invalid_argument ("HarmonicContext requires a scale definition name");
}

music_theory::PitchClass HarmonicContext::keyCenter() const noexcept
{
    return keyCenter_;
}

const std::string& HarmonicContext::scaleDefinitionName() const noexcept
{
    return scaleDefinitionName_;
}

music_theory::ScaleInstance HarmonicContext::scaleInstance (const music_theory::ScaleLibrary& scaleLibrary) const
{
    return scaleLibrary.instantiate (scaleDefinitionName_, spellKeyCenter (keyCenter_));
}

bool HarmonicContext::contains (music_theory::PitchClass pitchClass, const music_theory::ScaleLibrary& scaleLibrary) const
{
    return scaleInstance (scaleLibrary).contains (pitchClass);
}

bool operator== (const HarmonicContext& lhs, const HarmonicContext& rhs) noexcept
{
    return lhs.keyCenter() == rhs.keyCenter() && lhs.scaleDefinitionName() == rhs.scaleDefinitionName();
}

bool operator!= (const HarmonicContext& lhs, const HarmonicContext& rhs) noexcept
{
    return ! (lhs == rhs);
}
}
