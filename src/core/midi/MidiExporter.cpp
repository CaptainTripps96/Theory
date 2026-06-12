#include "core/midi/MidiExporter.h"

#include "core/midi/MidiFileWriter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace tsq::core::midi
{
namespace
{
std::string pathForMessage (const std::filesystem::path& filePath)
{
    const auto path = filePath.string();
    return path.empty() ? std::string { "(empty path)" } : path;
}

MidiFileEvent tempoEvent (const MidiExportOptions& options)
{
    const auto microsecondsPerQuarter = static_cast<int> (std::llround (options.tempo.secondsPerQuarterNote() * 1000000.0));

    return MidiFileEvent {
        0,
        0,
        {
            0xff,
            0x51,
            0x03,
            static_cast<std::uint8_t> ((microsecondsPerQuarter >> 16) & 0xff),
            static_cast<std::uint8_t> ((microsecondsPerQuarter >> 8) & 0xff),
            static_cast<std::uint8_t> (microsecondsPerQuarter & 0xff),
        }
    };
}

std::uint8_t denominatorPowerOfTwo (int denominator)
{
    if (denominator <= 0)
        throw std::invalid_argument ("Time signature denominator must be positive");

    auto value = denominator;
    auto power = std::uint8_t { 0 };
    while (value > 1)
    {
        if ((value % 2) != 0)
            throw std::invalid_argument ("Time signature denominator must be a power of two for MIDI export");

        value /= 2;
        ++power;
    }

    return power;
}

MidiFileEvent timeSignatureEvent (const MidiExportOptions& options)
{
    return MidiFileEvent {
        0,
        1,
        {
            0xff,
            0x58,
            0x04,
            static_cast<std::uint8_t> (options.timeSignature.numerator()),
            denominatorPowerOfTwo (options.timeSignature.denominator()),
            24,
            8,
        }
    };
}

int normalizedChannel (int channel)
{
    if (channel < 0 || channel > 15)
        throw std::invalid_argument ("MIDI export channel must be between 0 and 15");

    return channel;
}

MidiFileEvent noteOnEvent (const sequencing::MidiNote& note, int channel)
{
    return MidiFileEvent {
        note.startInClip().ticks(),
        3,
        {
            static_cast<std::uint8_t> (0x90 | channel),
            static_cast<std::uint8_t> (note.pitch().value()),
            static_cast<std::uint8_t> (note.velocity()),
        }
    };
}

MidiFileEvent noteOffEvent (const sequencing::MidiNote& note, int channel)
{
    return MidiFileEvent {
        note.endInClip().ticks(),
        2,
        {
            static_cast<std::uint8_t> (0x80 | channel),
            static_cast<std::uint8_t> (note.pitch().value()),
            0,
        }
    };
}
}

std::vector<std::uint8_t> MidiExporter::exportClipToBytes (const sequencing::MidiClip& clip, const MidiExportOptions& options)
{
    const auto channel = normalizedChannel (options.channel);
    std::vector<MidiFileEvent> events;

    if (options.includeTempoEvent)
        events.push_back (tempoEvent (options));

    if (options.includeTimeSignatureEvent)
        events.push_back (timeSignatureEvent (options));

    for (const auto& note : clip.notes())
    {
        events.push_back (noteOffEvent (note, channel));
        events.push_back (noteOnEvent (note, channel));
    }

    return MidiFileWriter::writeFormat0 (time::ticksPerQuarterNote, std::move (events));
}

void MidiExporter::exportClipToFile (const sequencing::MidiClip& clip,
                                     const std::filesystem::path& filePath,
                                     const MidiExportOptions& options)
{
    const auto bytes = exportClipToBytes (clip, options);

    std::ofstream stream { filePath, std::ios::binary };
    if (! stream)
        throw std::runtime_error ("Could not open MIDI file for writing: " + pathForMessage (filePath));

    stream.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
    if (! stream)
        throw std::runtime_error ("MIDI file write failed: " + pathForMessage (filePath));
}

diagnostics::Result MidiExporter::tryExportClipToFile (const sequencing::MidiClip& clip,
                                                       const std::filesystem::path& filePath,
                                                       const MidiExportOptions& options)
{
    try
    {
        exportClipToFile (clip, filePath, options);
        return diagnostics::Result::success();
    }
    catch (const std::exception& error)
    {
        return diagnostics::Result::failure ("MIDI export failed: " + std::string { error.what() });
    }
    catch (...)
    {
        return diagnostics::Result::failure ("MIDI export failed: unknown error");
    }
}
}
