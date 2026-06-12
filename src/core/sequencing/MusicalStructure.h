#pragma once

#include "core/sequencing/ChordProgressionLane.h"
#include "core/sequencing/KeyCenterRegion.h"
#include "core/sequencing/ScaleModeRegion.h"

#include <string>
#include <vector>

namespace tsq::core::sequencing
{
class MusicalStructure
{
public:
    MusicalStructure();
    MusicalStructure (music_theory::PitchClass defaultKeyCenter, std::string defaultScaleDefinitionName);

    music_theory::PitchClass defaultKeyCenter() const noexcept;
    const std::string& defaultScaleDefinitionName() const noexcept;

    void addKeyCenterRegion (KeyCenterRegion region);
    void addScaleModeRegion (ScaleModeRegion region);
    void addChordRegion (ChordRegion region);
    bool removeKeyCenterRegion (const KeyCenterRegion& region);
    bool removeScaleModeRegion (const ScaleModeRegion& region);
    bool removeChordRegion (const ChordRegion& region);

    const std::vector<KeyCenterRegion>& keyCenterRegions() const noexcept;
    const std::vector<ScaleModeRegion>& scaleModeRegions() const noexcept;
    ChordProgressionLane& chordProgressionLane() noexcept;
    const ChordProgressionLane& chordProgressionLane() const noexcept;
    const std::vector<ChordRegion>& chordRegions() const noexcept;

    music_theory::PitchClass keyCenterAt (time::TickPosition position) const noexcept;
    std::string scaleDefinitionNameAt (time::TickPosition position) const;

private:
    music_theory::PitchClass defaultKeyCenter_ = music_theory::PitchClass::c();
    std::string defaultScaleDefinitionName_ = "Major";
    std::vector<KeyCenterRegion> keyCenterRegions_;
    std::vector<ScaleModeRegion> scaleModeRegions_;
    ChordProgressionLane chordProgressionLane_;
};
}
