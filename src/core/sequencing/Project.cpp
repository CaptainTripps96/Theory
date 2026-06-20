#include "core/sequencing/Project.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
Project::Project (std::string id, std::string name)
    : id_ (std::move (id)),
      name_ (std::move (name))
{
    if (id_.empty())
        throw std::invalid_argument ("Project requires a non-empty ID");

    if (name_.empty())
        throw std::invalid_argument ("Project requires a non-empty name");
}

const std::string& Project::id() const noexcept
{
    return id_;
}

const std::string& Project::name() const noexcept
{
    return name_;
}

MusicalStructure& Project::musicalStructure() noexcept
{
    return musicalStructure_;
}

const MusicalStructure& Project::musicalStructure() const noexcept
{
    return musicalStructure_;
}

time::TempoMap& Project::tempoMap() noexcept
{
    return tempoMap_;
}

const time::TempoMap& Project::tempoMap() const noexcept
{
    return tempoMap_;
}

time::TimeSignatureMap& Project::timeSignatureMap() noexcept
{
    return timeSignatureMap_;
}

const time::TimeSignatureMap& Project::timeSignatureMap() const noexcept
{
    return timeSignatureMap_;
}

time::ProjectRhythmSettings& Project::rhythmSettings() noexcept
{
    return rhythmSettings_;
}

const time::ProjectRhythmSettings& Project::rhythmSettings() const noexcept
{
    return rhythmSettings_;
}

void Project::addTrack (Track track)
{
    const auto duplicateId = std::any_of (tracks_.begin(), tracks_.end(), [&track] (const auto& existingTrack) {
        return existingTrack.id() == track.id();
    });

    if (duplicateId)
        throw std::invalid_argument ("Project already contains a track with this ID");

    if (track.type() == TrackType::master && masterTrack() != nullptr)
        throw std::invalid_argument ("Project can contain only one master track");

    tracks_.push_back (std::move (track));
}

Track Project::removeTrackById (const std::string& trackId)
{
    const auto match = std::find_if (tracks_.begin(), tracks_.end(), [&trackId] (const auto& track) {
        return track.id() == trackId;
    });

    if (match == tracks_.end())
        throw std::invalid_argument ("Project does not contain a track with this ID");

    auto removedTrack = std::move (*match);
    tracks_.erase (match);
    return removedTrack;
}

const std::vector<Track>& Project::tracks() const noexcept
{
    return tracks_;
}

Track* Project::findTrackById (const std::string& id) noexcept
{
    const auto match = std::find_if (tracks_.begin(), tracks_.end(), [&id] (const auto& track) {
        return track.id() == id;
    });

    if (match == tracks_.end())
        return nullptr;

    return &*match;
}

const Track* Project::findTrackById (const std::string& id) const noexcept
{
    const auto match = std::find_if (tracks_.begin(), tracks_.end(), [&id] (const auto& track) {
        return track.id() == id;
    });

    if (match == tracks_.end())
        return nullptr;

    return &*match;
}

Track* Project::masterTrack() noexcept
{
    const auto match = std::find_if (tracks_.begin(), tracks_.end(), [] (const auto& track) {
        return track.type() == TrackType::master;
    });

    return match == tracks_.end() ? nullptr : &*match;
}

const Track* Project::masterTrack() const noexcept
{
    const auto match = std::find_if (tracks_.begin(), tracks_.end(), [] (const auto& track) {
        return track.type() == TrackType::master;
    });

    return match == tracks_.end() ? nullptr : &*match;
}

std::vector<const Track*> Project::tracksOfType (TrackType type) const
{
    std::vector<const Track*> result;
    for (const auto& track : tracks_)
        if (track.type() == type)
            result.push_back (&track);

    return result;
}

void Project::addCustomScale (music_theory::ScaleDefinition scale)
{
    const auto duplicateName = std::any_of (customScales_.begin(), customScales_.end(), [&scale] (const auto& existingScale) {
        return existingScale.name() == scale.name();
    });

    if (duplicateName)
        throw std::invalid_argument ("Project already contains a custom scale with this name");

    customScales_.push_back (std::move (scale));
}

music_theory::ScaleDefinition Project::removeCustomScaleByName (const std::string& name)
{
    const auto match = std::find_if (customScales_.begin(), customScales_.end(), [&name] (const auto& scale) {
        return scale.name() == name;
    });

    if (match == customScales_.end())
        throw std::invalid_argument ("Project does not contain a custom scale with this name");

    auto removedScale = std::move (*match);
    customScales_.erase (match);
    return removedScale;
}

const std::vector<music_theory::ScaleDefinition>& Project::customScales() const noexcept
{
    return customScales_;
}
}
