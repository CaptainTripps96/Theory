#include "core/midi/MidiExporter.h"

#include "core/midi/MidiFileWriter.h"
#include "core/sequencing/PreparedExpressionRenderModel.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

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

std::uint8_t midiSevenBitValue (double value) noexcept
{
    if (! std::isfinite (value))
        value = 0.0;

    return static_cast<std::uint8_t> (std::clamp (static_cast<int> (std::llround (value)), 0, 127));
}

MidiFileEvent controlChangeEvent (time::TickPosition position, int channel, int controller, double value)
{
    return MidiFileEvent {
        position.ticks(),
        4,
        {
            static_cast<std::uint8_t> (0xb0 | channel),
            static_cast<std::uint8_t> (std::clamp (controller, 0, 127)),
            midiSevenBitValue (value),
        }
    };
}

void addWarning (MidiExportReport* report, std::string warning)
{
    if (report != nullptr)
        report->warnings.push_back (std::move (warning));
}

bool destinationCanRenderToPlainMidi (const sequencing::ExpressionDestination& destination)
{
    return sequencing::expressionDestinationRuntimeSupport (destination).plainMidiExportMapped;
}

std::string unsupportedDestinationWarning (const sequencing::ExpressionDestination& destination)
{
    switch (destination.kind)
    {
        case sequencing::ExpressionDestinationKind::trackVolume:
        case sequencing::ExpressionDestinationKind::trackPan:
        case sequencing::ExpressionDestinationKind::sendLevel:
            return "Expression mixer route '" + destination.stableId() + "' was not exported to plain MIDI";

        case sequencing::ExpressionDestinationKind::pitch:
        case sequencing::ExpressionDestinationKind::pitchBend:
            return "Expression pitch route '" + destination.stableId() + "' was not exported to plain MIDI";

        case sequencing::ExpressionDestinationKind::firstPartyParameter:
            return "First-party expression route '" + destination.stableId() + "' was preserved in the project but not baked into plain MIDI";

        case sequencing::ExpressionDestinationKind::pluginParameter:
            return "Plugin parameter expression route '" + destination.stableId() + "' was preserved in the project but not baked into plain MIDI";

        case sequencing::ExpressionDestinationKind::midiCc:
            break;
    }

    return {};
}

void appendExpressionEventsForClip (const sequencing::Project& project,
                                    const sequencing::Track& track,
                                    const sequencing::MidiClip& clip,
                                    const MidiExportOptions& options,
                                    int channel,
                                    std::vector<MidiFileEvent>& events,
                                    MidiExportReport* report)
{
    if (options.expressionRenderStep.ticks() <= 0)
        throw std::invalid_argument ("MIDI expression export step must be positive");

    const auto prepared = sequencing::prepareExpressionClipRenderData (project,
                                                                       track,
                                                                       clip,
                                                                       options.expressionRenderStep,
                                                                       {});

    for (const auto& lane : prepared.lanes)
    {
        if (! lane.pitchSlurs.empty())
            addWarning (report, "Pitch slur expression data was preserved in the project but not baked into plain MIDI");
        if (! lane.vibratoEvents.empty())
            addWarning (report, "Vibrato expression data was preserved in the project but not baked into plain MIDI");

        for (const auto& route : lane.routes)
        {
            if (! route.available)
            {
                addWarning (report, "Unavailable expression route '" + route.destinationStableId + "' was not exported to plain MIDI");
                continue;
            }

            if (! destinationCanRenderToPlainMidi (route.destination))
            {
                const auto warning = unsupportedDestinationWarning (route.destination);
                if (! warning.empty())
                    addWarning (report, warning);
                continue;
            }

            if (! options.renderExpressionMidiCcRoutes)
            {
                addWarning (report, "MIDI CC expression route '" + route.destinationStableId + "' was skipped because expression MIDI CC export is disabled");
                continue;
            }

            if (route.destination.midiCcNumber < 0 || route.destination.midiCcNumber > 127)
            {
                addWarning (report, "MIDI CC expression route '" + route.destinationStableId + "' has an invalid controller number");
                continue;
            }

            auto lastValue = std::optional<std::uint8_t> {};
            auto lastTick = std::optional<std::int64_t> {};
            for (const auto& segment : route.outputSegments)
            {
                const auto startValue = midiSevenBitValue (segment.startValue);
                if (! lastValue.has_value() || *lastValue != startValue || ! lastTick.has_value() || *lastTick != segment.start.ticks())
                {
                    events.push_back (controlChangeEvent (segment.start, channel, route.destination.midiCcNumber, segment.startValue));
                    lastValue = startValue;
                    lastTick = segment.start.ticks();
                }

                const auto endValue = midiSevenBitValue (segment.endValue);
                if (endValue != *lastValue || segment.end.ticks() != *lastTick)
                {
                    events.push_back (controlChangeEvent (segment.end, channel, route.destination.midiCcNumber, segment.endValue));
                    lastValue = endValue;
                    lastTick = segment.end.ticks();
                }
            }
        }
    }
}

std::vector<std::uint8_t> exportEventsToBytes (const sequencing::MidiClip& clip,
                                               const MidiExportOptions& options,
                                               std::vector<MidiFileEvent> events,
                                               int channel)
{
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
}

std::vector<std::uint8_t> MidiExporter::exportClipToBytes (const sequencing::MidiClip& clip, const MidiExportOptions& options)
{
    const auto channel = normalizedChannel (options.channel);
    return exportEventsToBytes (clip, options, {}, channel);
}

std::vector<std::uint8_t> MidiExporter::exportClipToBytes (const sequencing::Project& project,
                                                           const std::string& trackId,
                                                           const sequencing::MidiClip& clip,
                                                           const MidiExportOptions& options,
                                                           MidiExportReport* report)
{
    if (report != nullptr)
        report->warnings.clear();

    const auto channel = normalizedChannel (options.channel);
    std::vector<MidiFileEvent> events;

    const auto* track = project.findTrackById (trackId);
    if (track == nullptr)
        throw std::invalid_argument ("MIDI expression export track was not found");

    appendExpressionEventsForClip (project, *track, clip, options, channel, events, report);
    return exportEventsToBytes (clip, options, std::move (events), channel);
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

void MidiExporter::exportClipToFile (const sequencing::Project& project,
                                     const std::string& trackId,
                                     const sequencing::MidiClip& clip,
                                     const std::filesystem::path& filePath,
                                     const MidiExportOptions& options,
                                     MidiExportReport* report)
{
    const auto bytes = exportClipToBytes (project, trackId, clip, options, report);

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

diagnostics::Result MidiExporter::tryExportClipToFile (const sequencing::Project& project,
                                                       const std::string& trackId,
                                                       const sequencing::MidiClip& clip,
                                                       const std::filesystem::path& filePath,
                                                       const MidiExportOptions& options,
                                                       MidiExportReport* report)
{
    try
    {
        exportClipToFile (project, trackId, clip, filePath, options, report);
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
