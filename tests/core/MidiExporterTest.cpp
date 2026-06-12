#include "core/midi/MidiExporter.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
using namespace tsq::core;
using namespace tsq::core::midi;
using namespace tsq::core::music_theory;
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

struct ParsedMidiEvent
{
    std::int64_t tick = 0;
    std::vector<std::uint8_t> data;
};

TickDuration beats (int count)
{
    return TickDuration::fromTicks (static_cast<std::int64_t> (count) * ticksPerQuarterNote);
}

TickPosition beat (int zeroBasedBeat)
{
    return TickPosition::fromTicks (static_cast<std::int64_t> (zeroBasedBeat) * ticksPerQuarterNote);
}

MidiClip clipWithCEG()
{
    MidiClip clip { "clip-1", "Triad", TickPosition {}, beats (2) };
    clip.addNote (MidiNote { "c", MidiPitch::fromValue (60), beat (0), beats (1), 100, NoteName::c() });
    clip.addNote (MidiNote { "e", MidiPitch::fromValue (64), beat (0), beats (1), 96, NoteName::e() });
    clip.addNote (MidiNote { "g", MidiPitch::fromValue (67), beat (1), beats (1), 90, NoteName::g() });
    return clip;
}

std::uint32_t readUint32BE (const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t> (bytes[offset]) << 24)
        | (static_cast<std::uint32_t> (bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t> (bytes[offset + 2]) << 8)
        | static_cast<std::uint32_t> (bytes[offset + 3]);
}

std::uint16_t readUint16BE (const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t> ((static_cast<std::uint16_t> (bytes[offset]) << 8) | bytes[offset + 1]);
}

std::int64_t readVariableLengthQuantity (const std::vector<std::uint8_t>& bytes, std::size_t& offset)
{
    auto value = std::int64_t { 0 };

    while (true)
    {
        const auto byte = bytes[offset++];
        value = (value << 7) | static_cast<std::int64_t> (byte & 0x7f);

        if ((byte & 0x80) == 0)
            return value;
    }
}

std::vector<ParsedMidiEvent> parseTrackEvents (const std::vector<std::uint8_t>& bytes)
{
    REQUIRE (std::string { reinterpret_cast<const char*> (bytes.data()), 4 } == "MThd");
    REQUIRE (readUint32BE (bytes, 4) == 6);
    REQUIRE (readUint16BE (bytes, 8) == 0);
    REQUIRE (readUint16BE (bytes, 10) == 1);
    REQUIRE (std::string { reinterpret_cast<const char*> (bytes.data() + 14), 4 } == "MTrk");

    const auto trackLength = readUint32BE (bytes, 18);
    auto offset = std::size_t { 22 };
    const auto trackEnd = offset + trackLength;
    auto tick = std::int64_t { 0 };
    std::vector<ParsedMidiEvent> events;

    while (offset < trackEnd)
    {
        tick += readVariableLengthQuantity (bytes, offset);
        const auto status = bytes[offset++];

        if (status == 0xff)
        {
            const auto type = bytes[offset++];
            const auto length = readVariableLengthQuantity (bytes, offset);
            std::vector<std::uint8_t> data { 0xff, type };
            for (auto index = std::int64_t { 0 }; index < length; ++index)
                data.push_back (bytes[offset++]);

            events.push_back (ParsedMidiEvent { tick, std::move (data) });
            continue;
        }

        std::vector<std::uint8_t> data { status, bytes[offset++], bytes[offset++] };
        events.push_back (ParsedMidiEvent { tick, std::move (data) });
    }

    return events;
}

int countStatus (const std::vector<ParsedMidiEvent>& events, std::uint8_t status)
{
    auto count = 0;
    for (const auto& event : events)
    {
        if (! event.data.empty() && event.data[0] == status)
            ++count;
    }
    return count;
}

bool hasEvent (const std::vector<ParsedMidiEvent>& events, std::int64_t tick, std::vector<std::uint8_t> data)
{
    for (const auto& event : events)
    {
        if (event.tick == tick && event.data == data)
            return true;
    }

    return false;
}
}

TEST_CASE ("MIDI exporter writes a standard MIDI header with 960 PPQ")
{
    const auto bytes = MidiExporter::exportClipToBytes (clipWithCEG());

    REQUIRE (bytes.size() > 22);
    CHECK (std::string { reinterpret_cast<const char*> (bytes.data()), 4 } == "MThd");
    CHECK (readUint16BE (bytes, 8) == 0);
    CHECK (readUint16BE (bytes, 10) == 1);
    CHECK (readUint16BE (bytes, 12) == ticksPerQuarterNote);
}

TEST_CASE ("MIDI exporter writes note timing pitch velocity and durations")
{
    const auto events = parseTrackEvents (MidiExporter::exportClipToBytes (clipWithCEG()));

    CHECK (hasEvent (events, 0, { 0x90, 60, 100 }));
    CHECK (hasEvent (events, 0, { 0x90, 64, 96 }));
    CHECK (hasEvent (events, ticksPerQuarterNote, { 0x80, 60, 0 }));
    CHECK (hasEvent (events, ticksPerQuarterNote, { 0x80, 64, 0 }));
    CHECK (hasEvent (events, ticksPerQuarterNote, { 0x90, 67, 90 }));
    CHECK (hasEvent (events, ticksPerQuarterNote * 2, { 0x80, 67, 0 }));
}

TEST_CASE ("MIDI exporter includes tempo and time signature meta events")
{
    MidiExportOptions options;
    options.tempo = Tempo { 120.0 };
    options.timeSignature = TimeSignature { 4, 4 };

    const auto events = parseTrackEvents (MidiExporter::exportClipToBytes (clipWithCEG(), options));

    CHECK (hasEvent (events, 0, { 0xff, 0x51, 0x07, 0xa1, 0x20 }));
    CHECK (hasEvent (events, 0, { 0xff, 0x58, 0x04, 0x02, 0x18, 0x08 }));
}

TEST_CASE ("MIDI exporter supports empty clips")
{
    const MidiClip emptyClip { "clip-empty", "Empty", TickPosition {}, beats (4) };
    const auto events = parseTrackEvents (MidiExporter::exportClipToBytes (emptyClip));

    CHECK (countStatus (events, 0x90) == 0);
    CHECK (countStatus (events, 0x80) == 0);
    CHECK (hasEvent (events, 0, { 0xff, 0x51, 0x07, 0xa1, 0x20 }));
    CHECK (hasEvent (events, 0, { 0xff, 0x2f }));
}

TEST_CASE ("MIDI exporter can write a file")
{
    const auto filePath = std::filesystem::temp_directory_path() / "TheorySequencerMidiExportTest.mid";
    std::filesystem::remove (filePath);

    MidiExporter::exportClipToFile (clipWithCEG(), filePath);

    REQUIRE (std::filesystem::is_regular_file (filePath));
    std::ifstream stream { filePath, std::ios::binary };
    std::vector<std::uint8_t> bytes {
        std::istreambuf_iterator<char> { stream },
        std::istreambuf_iterator<char> {}
    };

    CHECK (std::string { reinterpret_cast<const char*> (bytes.data()), 4 } == "MThd");
    std::filesystem::remove (filePath);
}

TEST_CASE ("MIDI exporter reports file write failures without throwing")
{
    const auto missingDirectory = std::filesystem::temp_directory_path() / "TheorySequencerMissingMidiExportDirectory";
    std::filesystem::remove_all (missingDirectory);

    const auto result = MidiExporter::tryExportClipToFile (clipWithCEG(), missingDirectory / "nested" / "export.mid");

    CHECK (result.failed());
    CHECK (result.error().find ("MIDI export failed") != std::string::npos);
    CHECK (result.error().find ("Could not open MIDI file for writing") != std::string::npos);
}
