#include "core/midi/MidiFileWriter.h"
#include "core/midi/MidiImporter.h"
#include "core/time/Tick.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
using namespace tsq::core::midi;
using namespace tsq::core::time;

MidiImportOptions importOptions()
{
    return MidiImportOptions {
        "imported-clip",
        "Imported MIDI",
        TickPosition {}
    };
}

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

void appendVlq (std::vector<std::uint8_t>& bytes, std::int64_t value)
{
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

std::vector<std::uint8_t> runningStatusMidiFile()
{
    std::vector<std::uint8_t> track;
    appendVlq (track, 0);
    track.push_back (0x90);
    track.push_back (60);
    track.push_back (100);
    appendVlq (track, 240);
    track.push_back (64);
    track.push_back (96);
    appendVlq (track, 240);
    track.push_back (60);
    track.push_back (0);
    appendVlq (track, 240);
    track.push_back (64);
    track.push_back (0);
    appendVlq (track, 0);
    track.push_back (0xff);
    track.push_back (0x2f);
    track.push_back (0x00);

    std::vector<std::uint8_t> bytes;
    appendAscii (bytes, "MThd");
    appendUint32BE (bytes, 6);
    appendUint16BE (bytes, 0);
    appendUint16BE (bytes, 1);
    appendUint16BE (bytes, 480);
    appendAscii (bytes, "MTrk");
    appendUint32BE (bytes, static_cast<std::uint32_t> (track.size()));
    bytes.insert (bytes.end(), track.begin(), track.end());
    return bytes;
}

std::vector<std::uint8_t> format1MidiFile()
{
    std::vector<std::uint8_t> trackOne;
    appendVlq (trackOne, 0);
    trackOne.push_back (0x90);
    trackOne.push_back (60);
    trackOne.push_back (100);
    appendVlq (trackOne, 960);
    trackOne.push_back (0x80);
    trackOne.push_back (60);
    trackOne.push_back (0);
    appendVlq (trackOne, 0);
    trackOne.push_back (0xff);
    trackOne.push_back (0x2f);
    trackOne.push_back (0x00);

    std::vector<std::uint8_t> trackTwo;
    appendVlq (trackTwo, 480);
    trackTwo.push_back (0x90);
    trackTwo.push_back (64);
    trackTwo.push_back (96);
    appendVlq (trackTwo, 960);
    trackTwo.push_back (0x80);
    trackTwo.push_back (64);
    trackTwo.push_back (0);
    appendVlq (trackTwo, 0);
    trackTwo.push_back (0xff);
    trackTwo.push_back (0x2f);
    trackTwo.push_back (0x00);

    std::vector<std::uint8_t> bytes;
    appendAscii (bytes, "MThd");
    appendUint32BE (bytes, 6);
    appendUint16BE (bytes, 1);
    appendUint16BE (bytes, 2);
    appendUint16BE (bytes, 960);
    appendAscii (bytes, "MTrk");
    appendUint32BE (bytes, static_cast<std::uint32_t> (trackOne.size()));
    bytes.insert (bytes.end(), trackOne.begin(), trackOne.end());
    appendAscii (bytes, "MTrk");
    appendUint32BE (bytes, static_cast<std::uint32_t> (trackTwo.size()));
    bytes.insert (bytes.end(), trackTwo.begin(), trackTwo.end());
    return bytes;
}
}

TEST_CASE ("MIDI importer converts source PPQ timing into app ticks")
{
    const auto bytes = MidiFileWriter::writeFormat0 (480,
                                                     {
                                                         MidiFileEvent { 480, 0, { 0x90, 60, 100 } },
                                                         MidiFileEvent { 960, 0, { 0x80, 60, 0 } },
                                                     });

    const auto result = MidiImporter::importClipFromBytes (bytes, importOptions());

    CHECK (result.sourcePulsesPerQuarterNote == 480);
    CHECK (result.sourceTrackCount == 1);
    REQUIRE (result.clip.notes().size() == 1);
    CHECK (result.clip.notes()[0].pitch().value() == 60);
    CHECK (result.clip.notes()[0].velocity() == 100);
    CHECK (result.clip.notes()[0].startInClip() == TickPosition::fromTicks (ticksPerQuarterNote));
    CHECK (result.clip.notes()[0].duration() == TickDuration::fromTicks (ticksPerQuarterNote));
    CHECK (result.clip.length() == TickDuration::fromTicks (ticksPerQuarterNote * 2));
}

TEST_CASE ("MIDI importer treats note-on velocity zero as note-off")
{
    const auto bytes = MidiFileWriter::writeFormat0 (960,
                                                     {
                                                         MidiFileEvent { 0, 0, { 0x90, 67, 88 } },
                                                         MidiFileEvent { 480, 0, { 0x90, 67, 0 } },
                                                     });

    const auto result = MidiImporter::importClipFromBytes (bytes, importOptions());

    REQUIRE (result.clip.notes().size() == 1);
    CHECK (result.clip.notes()[0].pitch().value() == 67);
    CHECK (result.clip.notes()[0].duration() == TickDuration::fromTicks (ticksPerQuarterNote / 2));
}

TEST_CASE ("MIDI importer supports running status channel events")
{
    const auto result = MidiImporter::importClipFromBytes (runningStatusMidiFile(), importOptions());

    REQUIRE (result.clip.notes().size() == 2);
    CHECK (result.clip.notes()[0].pitch().value() == 60);
    CHECK (result.clip.notes()[0].startInClip() == TickPosition {});
    CHECK (result.clip.notes()[0].duration() == TickDuration::fromTicks (ticksPerQuarterNote));
    CHECK (result.clip.notes()[1].pitch().value() == 64);
    CHECK (result.clip.notes()[1].startInClip() == TickPosition::fromTicks (ticksPerQuarterNote / 2));
    CHECK (result.clip.notes()[1].duration() == TickDuration::fromTicks (ticksPerQuarterNote));
}

TEST_CASE ("MIDI importer merges format-1 source tracks into one clip")
{
    const auto result = MidiImporter::importClipFromBytes (format1MidiFile(), importOptions());

    CHECK (result.sourceTrackCount == 2);
    REQUIRE (result.clip.notes().size() == 2);
    CHECK (result.clip.notes()[0].pitch().value() == 60);
    CHECK (result.clip.notes()[0].startInClip() == TickPosition {});
    CHECK (result.clip.notes()[1].pitch().value() == 64);
    CHECK (result.clip.notes()[1].startInClip() == TickPosition::fromTicks (ticksPerQuarterNote / 2));
    CHECK (result.clip.length() == TickDuration::fromTicks (ticksPerQuarterNote + (ticksPerQuarterNote / 2)));
}

TEST_CASE ("MIDI importer reads a standard MIDI file from disk")
{
    const auto filePath = std::filesystem::temp_directory_path() / "TheorySequencerMidiImportTest.mid";
    std::filesystem::remove (filePath);

    const auto bytes = MidiFileWriter::writeFormat0 (960,
                                                     {
                                                         MidiFileEvent { 0, 0, { 0x90, 72, 110 } },
                                                         MidiFileEvent { 960, 0, { 0x80, 72, 0 } },
                                                     });
    {
        std::ofstream stream { filePath, std::ios::binary };
        stream.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
    }

    const auto result = MidiImporter::importClipFromFile (filePath, importOptions());

    REQUIRE (result.clip.notes().size() == 1);
    CHECK (result.clip.notes()[0].pitch().value() == 72);
    std::filesystem::remove (filePath);
}

TEST_CASE ("MIDI importer rejects corrupt MIDI files")
{
    CHECK_THROWS_AS (MidiImporter::importClipFromBytes ({ 'M', 'T', 'h', 'd' }, importOptions()), std::runtime_error);
    CHECK_THROWS_AS (MidiImporter::importClipFromBytes ({ 'n', 'o', 'p', 'e' }, importOptions()), std::runtime_error);
}
