#pragma once

#include "core/music_theory/ScaleDefinition.h"
#include "core/sequencing/MusicalStructure.h"
#include "core/sequencing/Track.h"
#include "core/time/ProjectRhythmSettings.h"
#include "core/time/TempoMap.h"
#include "core/time/TimeSignatureMap.h"

#include <string>
#include <vector>

namespace tsq::core::sequencing
{
class Project
{
public:
    Project (std::string id, std::string name);

    const std::string& id() const noexcept;
    const std::string& name() const noexcept;

    MusicalStructure& musicalStructure() noexcept;
    const MusicalStructure& musicalStructure() const noexcept;
    time::TempoMap& tempoMap() noexcept;
    const time::TempoMap& tempoMap() const noexcept;
    time::TimeSignatureMap& timeSignatureMap() noexcept;
    const time::TimeSignatureMap& timeSignatureMap() const noexcept;
    time::ProjectRhythmSettings& rhythmSettings() noexcept;
    const time::ProjectRhythmSettings& rhythmSettings() const noexcept;

    void addTrack (Track track);
    Track removeTrackById (const std::string& trackId);
    const std::vector<Track>& tracks() const noexcept;
    Track* findTrackById (const std::string& id) noexcept;
    const Track* findTrackById (const std::string& id) const noexcept;
    Track* masterTrack() noexcept;
    const Track* masterTrack() const noexcept;
    std::vector<const Track*> tracksOfType (TrackType type) const;

    void addCustomScale (music_theory::ScaleDefinition scale);
    music_theory::ScaleDefinition removeCustomScaleByName (const std::string& name);
    const std::vector<music_theory::ScaleDefinition>& customScales() const noexcept;

private:
    std::string id_;
    std::string name_;
    MusicalStructure musicalStructure_;
    time::TempoMap tempoMap_;
    time::TimeSignatureMap timeSignatureMap_;
    time::ProjectRhythmSettings rhythmSettings_;
    std::vector<Track> tracks_;
    std::vector<music_theory::ScaleDefinition> customScales_;
};
}
