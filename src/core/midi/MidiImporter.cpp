#include "core/midi/MidiImporter.h"

#include "core/music_theory/MidiPitch.h"
#include "core/time/Tick.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tsq::core::midi
{
namespace
{
struct ByteReader
{
    const std::vector<std::uint8_t>& bytes;
    std::size_t offset = 0;

    [[nodiscard]] bool hasRemaining (std::size_t count) const noexcept
    {
        return count <= bytes.size() - std::min (offset, bytes.size());
    }

    void require (std::size_t count, const char* message) const
    {
        if (! hasRemaining (count))
            throw std::runtime_error (message);
    }

    std::uint8_t readByte (const char* message)
    {
        require (1, message);
        return bytes[offset++];
    }

    std::array<char, 4> readChunkId()
    {
        require (4, "MIDI file is truncated before a chunk ID");
        std::array<char, 4> id {};
        for (auto& character : id)
            character = static_cast<char> (bytes[offset++]);
        return id;
    }

    std::uint16_t readUint16()
    {
        require (2, "MIDI file is truncated before a 16-bit value");
        const auto value = static_cast<std::uint16_t> ((static_cast<std::uint16_t> (bytes[offset]) << 8)
                                                       | bytes[offset + 1]);
        offset += 2;
        return value;
    }

    std::uint32_t readUint32()
    {
        require (4, "MIDI file is truncated before a 32-bit value");
        const auto value = (static_cast<std::uint32_t> (bytes[offset]) << 24)
            | (static_cast<std::uint32_t> (bytes[offset + 1]) << 16)
            | (static_cast<std::uint32_t> (bytes[offset + 2]) << 8)
            | static_cast<std::uint32_t> (bytes[offset + 3]);
        offset += 4;
        return value;
    }
};

bool chunkIdEquals (const std::array<char, 4>& id, const char* text) noexcept
{
    return id[0] == text[0] && id[1] == text[1] && id[2] == text[2] && id[3] == text[3];
}

std::int64_t readVariableLengthQuantity (ByteReader& reader, std::size_t limit)
{
    auto value = std::int64_t { 0 };

    for (auto index = 0; index < 4; ++index)
    {
        if (reader.offset >= limit)
            throw std::runtime_error ("MIDI track is truncated in a delta-time value");

        const auto byte = reader.readByte ("MIDI track is truncated in a delta-time value");
        value = (value << 7) | static_cast<std::int64_t> (byte & 0x7f);

        if ((byte & 0x80) == 0)
            return value;
    }

    throw std::runtime_error ("MIDI variable-length quantity is too large");
}

std::int64_t toAppTicks (std::int64_t sourceTicks, int sourcePpq)
{
    if (sourceTicks <= 0)
        return 0;

    const auto scaled = (static_cast<long double> (sourceTicks)
                         * static_cast<long double> (time::ticksPerQuarterNote))
        / static_cast<long double> (sourcePpq);

    if (scaled > static_cast<long double> (std::numeric_limits<std::int64_t>::max()))
        throw std::runtime_error ("MIDI file is too long to import");

    return static_cast<std::int64_t> (std::llround (scaled));
}

int channelMessageDataLength (std::uint8_t status)
{
    switch (status & 0xf0)
    {
        case 0xc0:
        case 0xd0:
            return 1;

        case 0x80:
        case 0x90:
        case 0xa0:
        case 0xb0:
        case 0xe0:
            return 2;

        default:
            return 0;
    }
}

struct ActiveNote
{
    std::int64_t startTick = 0;
    int velocity = 100;
};

struct ImportedNote
{
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
    int channel = 0;
    int pitch = 60;
    int velocity = 100;
    std::size_t order = 0;
};

using ActiveNoteMap = std::map<int, std::deque<ActiveNote>>;

int activeNoteKey (int channel, int pitch) noexcept
{
    return (channel * 128) + pitch;
}

void closeActiveNote (ActiveNoteMap& activeNotes,
                      std::vector<ImportedNote>& notes,
                      std::int64_t tick,
                      int channel,
                      int pitch,
                      std::size_t& order)
{
    const auto key = activeNoteKey (channel, pitch);
    auto match = activeNotes.find (key);
    if (match == activeNotes.end() || match->second.empty())
        return;

    auto active = match->second.front();
    match->second.pop_front();
    if (match->second.empty())
        activeNotes.erase (match);

    notes.push_back (ImportedNote {
        active.startTick,
        std::max (tick, active.startTick + 1),
        channel,
        pitch,
        active.velocity,
        order++
    });
}

std::int64_t parseTrack (const std::vector<std::uint8_t>& bytes,
                         std::size_t trackStart,
                         std::size_t trackEnd,
                         std::vector<ImportedNote>& notes,
                         std::size_t& noteOrder)
{
    ByteReader reader { bytes, trackStart };
    auto absoluteTick = std::int64_t { 0 };
    auto lastTick = std::int64_t { 0 };
    auto runningStatus = std::uint8_t { 0 };
    ActiveNoteMap activeNotes;

    while (reader.offset < trackEnd)
    {
        absoluteTick += readVariableLengthQuantity (reader, trackEnd);
        lastTick = std::max (lastTick, absoluteTick);

        if (reader.offset >= trackEnd)
            throw std::runtime_error ("MIDI track is truncated before an event");

        auto statusOrData = reader.readByte ("MIDI track is truncated before an event");
        if (statusOrData == 0xff)
        {
            if (reader.offset >= trackEnd)
                throw std::runtime_error ("MIDI track is truncated in a meta event");

            const auto metaType = reader.readByte ("MIDI track is truncated in a meta event");
            const auto length = readVariableLengthQuantity (reader, trackEnd);
            if (length < 0 || static_cast<std::uint64_t> (length) > trackEnd - reader.offset)
                throw std::runtime_error ("MIDI track is truncated in a meta event payload");

            reader.offset += static_cast<std::size_t> (length);
            if (metaType == 0x2f)
                break;

            continue;
        }

        if (statusOrData == 0xf0 || statusOrData == 0xf7)
        {
            const auto length = readVariableLengthQuantity (reader, trackEnd);
            if (length < 0 || static_cast<std::uint64_t> (length) > trackEnd - reader.offset)
                throw std::runtime_error ("MIDI track is truncated in a SysEx event");

            reader.offset += static_cast<std::size_t> (length);
            continue;
        }

        auto status = statusOrData;
        auto firstDataByte = std::uint8_t { 0 };
        auto hasFirstDataByte = false;

        if ((statusOrData & 0x80) == 0)
        {
            if (runningStatus == 0)
                throw std::runtime_error ("MIDI running-status event appears before a channel status byte");

            status = runningStatus;
            firstDataByte = statusOrData;
            hasFirstDataByte = true;
        }
        else if (statusOrData < 0xf0)
        {
            runningStatus = statusOrData;
        }
        else
        {
            throw std::runtime_error ("MIDI file contains an unsupported system message");
        }

        const auto dataLength = channelMessageDataLength (status);
        if (dataLength == 0)
            throw std::runtime_error ("MIDI file contains an unsupported channel message");

        std::array<std::uint8_t, 2> data {};
        auto dataIndex = 0;
        if (hasFirstDataByte)
            data[dataIndex++] = firstDataByte;

        while (dataIndex < dataLength)
        {
            if (reader.offset >= trackEnd)
                throw std::runtime_error ("MIDI track is truncated in a channel event");

            const auto byte = reader.readByte ("MIDI track is truncated in a channel event");
            if ((byte & 0x80) != 0)
                throw std::runtime_error ("MIDI channel event payload contains a status byte");

            data[dataIndex++] = byte;
        }

        const auto channel = static_cast<int> (status & 0x0f);
        const auto messageKind = status & 0xf0;
        if (messageKind != 0x80 && messageKind != 0x90)
            continue;

        const auto pitch = static_cast<int> (data[0]);
        const auto velocity = dataLength > 1 ? static_cast<int> (data[1]) : 0;

        if (messageKind == 0x90 && velocity > 0)
        {
            activeNotes[activeNoteKey (channel, pitch)].push_back (ActiveNote { absoluteTick, velocity });
            continue;
        }

        closeActiveNote (activeNotes, notes, absoluteTick, channel, pitch, noteOrder);
    }

    for (auto& [key, pending] : activeNotes)
    {
        const auto channel = key / 128;
        const auto pitch = key % 128;
        while (! pending.empty())
        {
            const auto active = pending.front();
            pending.pop_front();
            notes.push_back (ImportedNote {
                active.startTick,
                std::max (lastTick, active.startTick + 1),
                channel,
                pitch,
                active.velocity,
                noteOrder++
            });
        }
    }

    return lastTick;
}

struct Header
{
    int format = 0;
    int trackCount = 0;
    int pulsesPerQuarterNote = 0;
};

Header readHeader (ByteReader& reader)
{
    if (! reader.hasRemaining (14))
        throw std::runtime_error ("MIDI file is too short");

    const auto chunkId = reader.readChunkId();
    if (! chunkIdEquals (chunkId, "MThd"))
        throw std::runtime_error ("MIDI file does not start with an MThd header");

    const auto headerLength = reader.readUint32();
    if (headerLength < 6)
        throw std::runtime_error ("MIDI header length is invalid");

    reader.require (headerLength, "MIDI file is truncated in the header");

    const auto format = static_cast<int> (reader.readUint16());
    const auto trackCount = static_cast<int> (reader.readUint16());
    const auto division = static_cast<int> (reader.readUint16());

    if (format < 0 || format > 1)
        throw std::runtime_error ("Only MIDI file formats 0 and 1 are supported");

    if (trackCount <= 0)
        throw std::runtime_error ("MIDI file does not contain any tracks");

    if ((division & 0x8000) != 0)
        throw std::runtime_error ("SMPTE-time MIDI files are not supported");

    if (division <= 0)
        throw std::runtime_error ("MIDI pulses-per-quarter-note division is invalid");

    reader.offset += static_cast<std::size_t> (headerLength - 6);
    return Header { format, trackCount, division };
}

std::vector<ImportedNote> readNotes (const std::vector<std::uint8_t>& bytes,
                                     ByteReader& reader,
                                     const Header& header,
                                     std::int64_t& lastSourceTick)
{
    auto sourceTracksRead = 0;
    auto noteOrder = std::size_t { 0 };
    std::vector<ImportedNote> importedNotes;

    while (reader.offset < bytes.size() && sourceTracksRead < header.trackCount)
    {
        const auto chunkId = reader.readChunkId();
        const auto chunkLength = static_cast<std::size_t> (reader.readUint32());
        reader.require (chunkLength, "MIDI file is truncated in a chunk payload");

        const auto chunkStart = reader.offset;
        const auto chunkEnd = chunkStart + chunkLength;

        if (chunkIdEquals (chunkId, "MTrk"))
        {
            lastSourceTick = std::max (lastSourceTick,
                                       parseTrack (bytes, chunkStart, chunkEnd, importedNotes, noteOrder));
            ++sourceTracksRead;
        }

        reader.offset = chunkEnd;
    }

    if (sourceTracksRead != header.trackCount)
        throw std::runtime_error ("MIDI file ended before all declared tracks were read");

    return importedNotes;
}

std::string noteId (std::size_t index)
{
    return "note-" + std::to_string (index + 1);
}

sequencing::MidiClip buildClip (std::vector<ImportedNote> notes,
                                std::int64_t lastSourceTick,
                                const Header& header,
                                MidiImportOptions options)
{
    if (options.clipId.empty())
        throw std::invalid_argument ("MIDI import requires a non-empty clip ID");

    if (options.clipName.empty())
        throw std::invalid_argument ("MIDI import requires a non-empty clip name");

    std::sort (notes.begin(), notes.end(), [] (const auto& lhs, const auto& rhs) {
        if (lhs.startTick != rhs.startTick)
            return lhs.startTick < rhs.startTick;
        if (lhs.pitch != rhs.pitch)
            return lhs.pitch < rhs.pitch;
        if (lhs.channel != rhs.channel)
            return lhs.channel < rhs.channel;
        return lhs.order < rhs.order;
    });

    auto clipEndTick = std::max<std::int64_t> (1, toAppTicks (lastSourceTick, header.pulsesPerQuarterNote));
    for (const auto& note : notes)
        clipEndTick = std::max (clipEndTick, toAppTicks (note.endTick, header.pulsesPerQuarterNote));

    sequencing::MidiClip clip {
        std::move (options.clipId),
        std::move (options.clipName),
        options.startInProject,
        time::TickDuration::fromTicks (clipEndTick)
    };

    for (auto index = std::size_t { 0 }; index < notes.size(); ++index)
    {
        const auto& note = notes[index];
        const auto start = toAppTicks (note.startTick, header.pulsesPerQuarterNote);
        const auto end = std::max (start + 1, toAppTicks (note.endTick, header.pulsesPerQuarterNote));

        clip.addNote (sequencing::MidiNote {
            noteId (index),
            music_theory::MidiPitch::fromValue (note.pitch),
            time::TickPosition::fromTicks (start),
            time::TickDuration::fromTicks (end - start),
            std::clamp (note.velocity, 1, 127)
        });
    }

    return clip;
}
}

MidiImportResult MidiImporter::importClipFromBytes (const std::vector<std::uint8_t>& bytes,
                                                    MidiImportOptions options)
{
    ByteReader reader { bytes };
    const auto header = readHeader (reader);
    auto lastSourceTick = std::int64_t { 0 };
    auto notes = readNotes (bytes, reader, header, lastSourceTick);
    const auto importedNoteCount = notes.size();

    auto clip = buildClip (std::move (notes), lastSourceTick, header, std::move (options));
    return MidiImportResult {
        std::move (clip),
        header.pulsesPerQuarterNote,
        importedNoteCount,
        static_cast<std::size_t> (header.trackCount)
    };
}

MidiImportResult MidiImporter::importClipFromFile (const std::filesystem::path& filePath,
                                                   MidiImportOptions options)
{
    if (filePath.empty())
        throw std::runtime_error ("No MIDI file path was provided");

    std::error_code error;
    if (! std::filesystem::is_regular_file (filePath, error))
        throw std::runtime_error ("MIDI file was not found: " + filePath.string());

    std::ifstream stream { filePath, std::ios::binary };
    if (! stream)
        throw std::runtime_error ("Could not open MIDI file: " + filePath.string());

    std::vector<std::uint8_t> bytes {
        std::istreambuf_iterator<char> { stream },
        std::istreambuf_iterator<char> {}
    };

    if (! stream.eof() && stream.fail())
        throw std::runtime_error ("Could not read MIDI file: " + filePath.string());

    return importClipFromBytes (bytes, std::move (options));
}
}
