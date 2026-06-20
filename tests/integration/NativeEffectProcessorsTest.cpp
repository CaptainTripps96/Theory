#include "app/AppServices.h"
#include "core/devices/FirstPartyDeviceRegistry.h"
#include "engine/devices/NativeEffectProcessors.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace
{
using namespace tsq::engine::devices;

void fillSine (juce::AudioBuffer<float>& buffer)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto frequency = 220.0;
    for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto value = static_cast<float> (std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (sample) / sampleRate) * 0.25);
        buffer.setSample (0, sample, value);
        buffer.setSample (1, sample, value * 0.9f);
    }
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (buffer.getSample (channel, sample)))
                return false;

    return true;
}

float absolutePeakAfter (const juce::AudioBuffer<float>& buffer, int firstSample)
{
    auto peak = 0.0f;
    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (auto sample = std::max (0, firstSample); sample < buffer.getNumSamples(); ++sample)
            peak = std::max (peak, std::abs (buffer.getSample (channel, sample)));

    return peak;
}

float accumulatedDifference (const juce::AudioBuffer<float>& lhs, const juce::AudioBuffer<float>& rhs)
{
    auto difference = 0.0f;
    for (auto channel = 0; channel < std::min (lhs.getNumChannels(), rhs.getNumChannels()); ++channel)
        for (auto sample = 0; sample < std::min (lhs.getNumSamples(), rhs.getNumSamples()); ++sample)
            difference += std::abs (lhs.getSample (channel, sample) - rhs.getSample (channel, sample));

    return difference;
}
}

TEST_CASE ("Native phaser processes finite audio and changes the source", "[integration][devices][native-effects]")
{
    juce::AudioBuffer<float> source { 2, 2048 };
    fillSine (source);
    auto processed = source;

    NativePhaserProcessor phaser;
    phaser.prepare (48000.0, processed.getNumSamples());
    phaser.setParameters (NativePhaserParameters { 1.0f, 0.8f });
    phaser.processBlock (processed, 0, processed.getNumSamples());

    CHECK (allFinite (processed));
    CHECK (accumulatedDifference (source, processed) > 0.001f);
}

TEST_CASE ("Native reverb creates a finite stereo tail from an impulse", "[integration][devices][native-effects]")
{
    juce::AudioBuffer<float> buffer { 2, 8192 };
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);

    NativeReverbProcessor reverb;
    reverb.prepare (48000.0, buffer.getNumSamples());
    reverb.setParameters (NativeReverbParameters { 0.65f, 0.85f });
    reverb.processBlock (buffer, 0, buffer.getNumSamples());

    CHECK (allFinite (buffer));
    CHECK (absolutePeakAfter (buffer, 1200) > 0.0001f);
}

TEST_CASE ("Native tape simulator processes finite audio and changes the source", "[integration][devices][native-effects]")
{
    juce::AudioBuffer<float> source { 2, 4096 };
    fillSine (source);
    auto processed = source;

    NativeTapeProcessor tape;
    tape.prepare (48000.0, processed.getNumSamples());
    tape.setParameters (NativeTapeParameters { 0.75f, 0.25f, 0.15f, 0.0f, 1.0f });
    tape.processBlock (processed, 0, processed.getNumSamples());

    CHECK (allFinite (processed));
    CHECK (absolutePeakAfter (processed, 0) > 0.001f);
    CHECK (accumulatedDifference (source, processed) > 0.001f);
}

TEST_CASE ("AppServices inserts native effects into a first-party MIDI device chain", "[integration][devices][native-effects][app-services]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    tsq::app::AppServices services;
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", tsq::core::devices::simpleOscComplexTypeId(), 0));
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", tsq::core::devices::nativePhaserTypeId(), 1));
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", tsq::core::devices::nativeReverbTypeId(), 2));
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", tsq::core::devices::nativeTapeSimulatorTypeId(), 3));
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    const auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    REQUIRE (track->deviceChain().slots().size() == 4);
    REQUIRE (track->deviceChain().slots()[1].firstPartyDevice().has_value());
    REQUIRE (track->deviceChain().slots()[2].firstPartyDevice().has_value());
    REQUIRE (track->deviceChain().slots()[3].firstPartyDevice().has_value());
    CHECK (track->deviceChain().slots()[1].firstPartyDevice()->typeId == tsq::core::devices::nativePhaserTypeId());
    CHECK (track->deviceChain().slots()[2].firstPartyDevice()->typeId == tsq::core::devices::nativeReverbTypeId());
    CHECK (track->deviceChain().slots()[3].firstPartyDevice()->typeId == tsq::core::devices::nativeTapeSimulatorTypeId());
}
