#include "core/midi/MidiFileWriter.h"

#include <algorithm>
#include <stdexcept>

namespace tsq::core::midi
{
namespace
{
void appendAscii (std::vector<std::uint8_t>& bytes, const char* text)
{
    while (*text != '\0')
    {
        bytes.push_back (static_cast<std::uint8_t> (*text));
        ++text;
    }
}

void appendUint16BE (std::vector<std::uint8_t>& bytes, int value)
{
    bytes.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
    bytes.push_back (static_cast<std::uint8_t> (value & 0xff));
}

void appendUint32BE (std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back (static_cast<std::uint8_t> ((value >> 24) & 0xff));
    bytes.push_back (static_cast<std::uint8_t> ((value >> 16) & 0xff));
    bytes.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
    bytes.push_back (static_cast<std::uint8_t> (value & 0xff));
}

void appendVariableLengthQuantity (std::vector<std::uint8_t>& bytes, std::int64_t value)
{
    if (value < 0)
        throw std::invalid_argument ("MIDI delta time must not be negative");

    auto buffer = static_cast<std::uint32_t> (value & 0x7f);
    value >>= 7;

    while (value > 0)
    {
        buffer <<= 8;
        buffer |= static_cast<std::uint32_t> ((value & 0x7f) | 0x80);
        value >>= 7;
    }

    while (true)
    {
        bytes.push_back (static_cast<std::uint8_t> (buffer & 0xff));
        if ((buffer & 0x80) == 0)
            break;

        buffer >>= 8;
    }
}

std::vector<std::uint8_t> writeTrackChunk (std::vector<MidiFileEvent> events)
{
    std::stable_sort (events.begin(), events.end(), [] (const auto& lhs, const auto& rhs) {
        if (lhs.tick != rhs.tick)
            return lhs.tick < rhs.tick;

        return lhs.priority < rhs.priority;
    });

    std::vector<std::uint8_t> track;
    auto previousTick = std::int64_t { 0 };

    for (const auto& event : events)
    {
        if (event.tick < previousTick)
            throw std::invalid_argument ("MIDI events must be sorted by non-negative ticks");

        appendVariableLengthQuantity (track, event.tick - previousTick);
        track.insert (track.end(), event.data.begin(), event.data.end());
        previousTick = event.tick;
    }

    appendVariableLengthQuantity (track, 0);
    track.push_back (0xff);
    track.push_back (0x2f);
    track.push_back (0x00);
    return track;
}
}

std::vector<std::uint8_t> MidiFileWriter::writeFormat0 (int pulsesPerQuarterNote, std::vector<MidiFileEvent> events)
{
    if (pulsesPerQuarterNote <= 0 || pulsesPerQuarterNote > 0x7fff)
        throw std::invalid_argument ("MIDI pulses-per-quarter-note division is out of range");

    std::vector<std::uint8_t> bytes;
    appendAscii (bytes, "MThd");
    appendUint32BE (bytes, 6);
    appendUint16BE (bytes, 0);
    appendUint16BE (bytes, 1);
    appendUint16BE (bytes, pulsesPerQuarterNote);

    const auto track = writeTrackChunk (std::move (events));
    appendAscii (bytes, "MTrk");
    appendUint32BE (bytes, static_cast<std::uint32_t> (track.size()));
    bytes.insert (bytes.end(), track.begin(), track.end());

    return bytes;
}
}
