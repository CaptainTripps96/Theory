#pragma once

#include <string>
#include <string_view>

namespace tsq::core::sequencing
{
enum class TrackType
{
    midi,
    audio,
    returnTrack,
    master
};

std::string trackTypeId (TrackType type);
TrackType trackTypeFromId (std::string_view id);
bool trackTypeCanOwnMidiClips (TrackType type) noexcept;
bool trackTypeCanOwnAudioClips (TrackType type) noexcept;
bool trackTypeCanRecordMidi (TrackType type) noexcept;
bool trackTypeCanHostInstrument (TrackType type) noexcept;
bool trackTypeCanHostAudioEffects (TrackType type) noexcept;
}
