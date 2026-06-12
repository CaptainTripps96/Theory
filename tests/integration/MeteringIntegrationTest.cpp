#include "app/AppServices.h"
#include "core/sequencing/AudioClip.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "core/time/Tick.h"
#include "engine/PlaybackEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
using namespace tsq;

core::time::TickDuration beats (int count)
{
    return core::time::TickDuration::fromTicks (static_cast<std::int64_t> (count) * core::time::ticksPerQuarterNote);
}

core::time::TickPosition beat (int zeroBasedBeat)
{
    return core::time::TickPosition::fromTicks (static_cast<std::int64_t> (zeroBasedBeat) * core::time::ticksPerQuarterNote);
}

std::filesystem::path uniqueWavePath()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("TheorySequencerMeterTest-" + std::to_string (stamp) + ".wav");
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

void writeSineWaveFile (const std::filesystem::path& path)
{
    constexpr auto sampleRate = 48000;
    constexpr auto channels = 2;
    constexpr auto bitsPerSample = 16;
    constexpr auto seconds = 2;
    constexpr auto frames = sampleRate * seconds;
    constexpr auto bytesPerSample = bitsPerSample / 8;
    constexpr auto dataBytes = frames * channels * bytesPerSample;
    constexpr auto pi = 3.14159265358979323846264338327950288;

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
        const auto sample = static_cast<std::int16_t> (std::sin (2.0 * pi * 440.0 * static_cast<double> (frame) / sampleRate) * 18000.0);
        for (auto channel = 0; channel < channels; ++channel)
            writeLittleEndian16 (stream, static_cast<std::uint16_t> (sample));
    }
}

void pumpMessagesFor (int milliseconds)
{
    auto* manager = juce::MessageManager::getInstance();
    REQUIRE (manager != nullptr);
    manager->runDispatchLoopUntil (milliseconds);
}

float peakForSource (const engine::MeterSnapshot& snapshot, const std::string& sourceId)
{
    for (const auto& source : snapshot.sources)
    {
        if (source.sourceId != sourceId)
            continue;

        auto peak = 0.0f;
        for (const auto& channel : source.channels)
            peak = std::max (peak, channel.peakLinear);

        return peak;
    }

    return 0.0f;
}

float masterPeak (const engine::MeterSnapshot& snapshot)
{
    for (const auto& source : snapshot.sources)
    {
        if (! source.master)
            continue;

        auto peak = 0.0f;
        for (const auto& channel : source.channels)
            peak = std::max (peak, channel.peakLinear);

        return peak;
    }

    return 0.0f;
}
}

TEST_CASE ("Playback meter snapshots are driven by rendered audio", "[integration][meter]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const auto wavePath = uniqueWavePath();
    writeSineWaveFile (wavePath);

    app::AppServices services;

    core::sequencing::Track audioTrack { "audio-meter-track", "Meter Audio", core::sequencing::TrackType::audio };
    auto strip = audioTrack.mixerStrip();
    strip.setMeterSourceId ("meter-audio-track");
    audioTrack.setMixerStrip (strip);

    audioTrack.addAudioClip (core::sequencing::AudioClip {
        "audio-meter-clip",
        "Meter Clip",
        core::sequencing::AudioSourceReference { "meter-source", wavePath.string(), "Meter Source", false },
        beat (0),
        beats (4)
    });

    services.project().addTrack (std::move (audioTrack));
    services.markPlaybackProjectDirty();

    auto stoppedSnapshot = services.playbackEngine().getMeterSnapshot();
    CHECK (peakForSource (stoppedSnapshot, "meter-audio-track") == 0.0f);

    REQUIRE (services.startProjectPlayback());

    auto sawTrackMeter = false;
    auto sawMasterMeter = false;
    for (auto attempt = 0; attempt < 30; ++attempt)
    {
        pumpMessagesFor (100);
        const auto snapshot = services.playbackEngine().getMeterSnapshot();
        sawTrackMeter = sawTrackMeter || peakForSource (snapshot, "meter-audio-track") > 0.01f;
        sawMasterMeter = sawMasterMeter || masterPeak (snapshot) > 0.01f;

        if (sawTrackMeter && sawMasterMeter)
            break;
    }

    services.stopProjectPlayback();
    const auto resetSnapshot = services.playbackEngine().getMeterSnapshot();

    CHECK (sawTrackMeter);
    CHECK (sawMasterMeter);
    CHECK (peakForSource (resetSnapshot, "meter-audio-track") == 0.0f);
    CHECK (masterPeak (resetSnapshot) == 0.0f);

    std::filesystem::remove (wavePath);
}
