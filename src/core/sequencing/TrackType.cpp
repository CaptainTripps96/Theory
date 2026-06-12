#include "core/sequencing/TrackType.h"

#include <stdexcept>

namespace tsq::core::sequencing
{
std::string trackTypeId (TrackType type)
{
    switch (type)
    {
        case TrackType::midi: return "midi";
        case TrackType::audio: return "audio";
        case TrackType::returnTrack: return "return";
        case TrackType::master: return "master";
    }

    return "midi";
}

TrackType trackTypeFromId (std::string_view id)
{
    if (id == "midi") return TrackType::midi;
    if (id == "audio") return TrackType::audio;
    if (id == "return") return TrackType::returnTrack;
    if (id == "master") return TrackType::master;

    throw std::invalid_argument ("Unknown track type '" + std::string { id } + "'");
}

bool trackTypeCanOwnMidiClips (TrackType type) noexcept
{
    return type == TrackType::midi;
}

bool trackTypeCanOwnAudioClips (TrackType type) noexcept
{
    return type == TrackType::audio;
}

bool trackTypeCanRecordMidi (TrackType type) noexcept
{
    return type == TrackType::midi;
}

bool trackTypeCanHostInstrument (TrackType type) noexcept
{
    return type == TrackType::midi;
}

bool trackTypeCanHostAudioEffects (TrackType type) noexcept
{
    return type == TrackType::midi
        || type == TrackType::audio
        || type == TrackType::returnTrack
        || type == TrackType::master;
}
}
