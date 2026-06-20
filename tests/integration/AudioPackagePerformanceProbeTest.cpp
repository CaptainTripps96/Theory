#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/serialization/ProjectPackage.h"
#include "core/serialization/ProjectSerializer.h"
#include "core/sequencing/AudioClip.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "core/time/Tick.h"
#include "ui/TimelineComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace
{
using namespace tsq;

core::time::TickPosition beat (int zeroBasedBeat)
{
    return core::time::TickPosition::fromTicks (
        static_cast<std::int64_t> (zeroBasedBeat) * core::time::ticksPerQuarterNote);
}

core::time::TickDuration beats (int count)
{
    return core::time::TickDuration::fromTicks (
        static_cast<std::int64_t> (count) * core::time::ticksPerQuarterNote);
}

std::filesystem::path uniqueTempPath (const std::string& stem, const std::string& extension)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / (stem + "-" + std::to_string (stamp) + extension);
}

void writeLittleEndian16 (std::ofstream& stream, std::uint16_t value)
{
    stream.put (static_cast<char> (value & 0xffu));
    stream.put (static_cast<char> ((value >> 8u) & 0xffu));
}

void writeLittleEndian32 (std::ofstream& stream, std::uint32_t value)
{
    stream.put (static_cast<char> (value & 0xffu));
    stream.put (static_cast<char> ((value >> 8u) & 0xffu));
    stream.put (static_cast<char> ((value >> 16u) & 0xffu));
    stream.put (static_cast<char> ((value >> 24u) & 0xffu));
}

void writeSineWaveFile (const std::filesystem::path& path, int seconds)
{
    constexpr auto sampleRate = 48000;
    constexpr auto channels = 2;
    constexpr auto bitsPerSample = 16;
    constexpr auto bytesPerSample = bitsPerSample / 8;
    constexpr auto pi = 3.14159265358979323846264338327950288;

    const auto frames = sampleRate * seconds;
    const auto dataBytes = frames * channels * bytesPerSample;

    std::ofstream stream { path, std::ios::binary };
    REQUIRE (stream.good());

    stream.write ("RIFF", 4);
    writeLittleEndian32 (stream, 36u + static_cast<std::uint32_t> (dataBytes));
    stream.write ("WAVE", 4);
    stream.write ("fmt ", 4);
    writeLittleEndian32 (stream, 16);
    writeLittleEndian16 (stream, 1);
    writeLittleEndian16 (stream, channels);
    writeLittleEndian32 (stream, sampleRate);
    writeLittleEndian32 (stream, sampleRate * channels * bytesPerSample);
    writeLittleEndian16 (stream, channels * bytesPerSample);
    writeLittleEndian16 (stream, bitsPerSample);
    stream.write ("data", 4);
    writeLittleEndian32 (stream, static_cast<std::uint32_t> (dataBytes));

    for (auto frame = 0; frame < frames; ++frame)
    {
        const auto sample = static_cast<std::int16_t> (
            std::sin (2.0 * pi * 220.0 * static_cast<double> (frame) / sampleRate) * 16000.0);
        for (auto channel = 0; channel < channels; ++channel)
            writeLittleEndian16 (stream, static_cast<std::uint16_t> (sample));
    }
}

core::sequencing::AudioSourceReference sharedAudioSource (const std::filesystem::path& wavePath)
{
    return core::sequencing::AudioSourceReference {
        "audio-package-perf-source",
        wavePath.string(),
        "Audio Package Perf Source",
        false
    };
}

core::sequencing::Project audioProject (int trackCount,
                                        int clipsPerTrack,
                                        const std::filesystem::path& wavePath)
{
    core::sequencing::Project project {
        "audio-package-perf-project",
        "Audio Package Perf Project"
    };

    const auto source = sharedAudioSource (wavePath);
    for (auto trackIndex = 0; trackIndex < trackCount; ++trackIndex)
    {
        core::sequencing::Track track {
            "audio-track-" + std::to_string (trackIndex + 1),
            "Audio Track " + std::to_string (trackIndex + 1),
            core::sequencing::TrackType::audio
        };

        for (auto clipIndex = 0; clipIndex < clipsPerTrack; ++clipIndex)
        {
            track.addAudioClip (core::sequencing::AudioClip {
                "audio-clip-" + std::to_string (trackIndex + 1) + "-" + std::to_string (clipIndex + 1),
                "Audio Clip " + std::to_string (clipIndex + 1),
                source,
                beat (clipIndex),
                beats (1)
            });
        }

        project.addTrack (std::move (track));
    }

    return project;
}

void replaceProject (app::AppServices& services, core::sequencing::Project source)
{
    auto& project = services.project();
    while (! project.tracks().empty())
        project.removeTrackById (project.tracks().back().id());

    for (auto track : source.tracks())
        project.addTrack (std::move (track));

    if (! project.tracks().empty())
        services.setSelectedTrack (project.tracks().front().id());
}

void pumpMessagesFor (int milliseconds)
{
    auto* manager = juce::MessageManager::getInstance();
    REQUIRE (manager != nullptr);
    manager->runDispatchLoopUntil (milliseconds);
}

void probeTimelinePaint (const core::sequencing::Project& project, int trackCount, int clipCount)
{
    app::AppServices services;
    replaceProject (services, project);

    ui::TimelineComponent timeline { services };
    const auto height = 260 + (trackCount * 96);
    timeline.setBounds (0, 0, 1600, height);

    juce::Image image { juce::Image::ARGB, 1600, height, true };
    juce::Graphics graphics { image };

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AudioPackagePerfProbe::timeline paint cold tracks=" + std::to_string (trackCount)
                + " clips=" + std::to_string (clipCount)
        };
        timeline.paintEntireComponent (graphics, true);
    }

    pumpMessagesFor (120);

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AudioPackagePerfProbe::timeline paint warm tracks=" + std::to_string (trackCount)
                + " clips=" + std::to_string (clipCount)
        };
        timeline.paintEntireComponent (graphics, true);
    }
}
}

TEST_CASE ("Audio file, waveform, package, and serialization paths are performance probed",
           "[integration][audio-package][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    constexpr auto trackCount = 16;
    constexpr auto clipsPerTrack = 32;
    constexpr auto clipCount = trackCount * clipsPerTrack;

    const auto wavePath = uniqueTempPath ("TheorySequencerAudioPackagePerf", ".wav");
    const auto packagePath = uniqueTempPath ("TheorySequencerAudioPackagePerf", ".tseq");
    writeSineWaveFile (wavePath, 4);
    std::filesystem::remove_all (packagePath);

    app::AppServices importServices;
    {
        core::diagnostics::ScopedPerformanceTimer timer { "AudioPackagePerfProbe::import audio file" };
        REQUIRE (importServices.createAudioTrackFromFile (wavePath, "Imported Perf Audio"));
    }

    const auto project = audioProject (trackCount, clipsPerTrack, wavePath);
    std::string serialized;
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AudioPackagePerfProbe::serialize tracks=" + std::to_string (trackCount)
                + " clips=" + std::to_string (clipCount)
        };
        serialized = core::serialization::ProjectSerializer::serialize (project);
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AudioPackagePerfProbe::deserialize tracks=" + std::to_string (trackCount)
                + " clips=" + std::to_string (clipCount)
        };
        const auto result = core::serialization::ProjectSerializer::deserializeWithWarnings (serialized);
        CHECK (static_cast<int> (result.project.tracks().size()) == trackCount);
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AudioPackagePerfProbe::package save tracks=" + std::to_string (trackCount)
                + " clips=" + std::to_string (clipCount)
        };
        core::serialization::ProjectPackage::save (project, packagePath);
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AudioPackagePerfProbe::package load present-source tracks=" + std::to_string (trackCount)
                + " clips=" + std::to_string (clipCount)
        };
        const auto result = core::serialization::ProjectPackage::loadWithWarnings (packagePath);
        CHECK (result.warnings.empty());
    }

    probeTimelinePaint (project, trackCount, clipCount);

    std::filesystem::remove (wavePath);

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AudioPackagePerfProbe::package load missing-source tracks=" + std::to_string (trackCount)
                + " clips=" + std::to_string (clipCount)
        };
        const auto result = core::serialization::ProjectPackage::loadWithWarnings (packagePath);
        CHECK_FALSE (result.warnings.empty());
    }

    std::filesystem::remove_all (packagePath);
}
