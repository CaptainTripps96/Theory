#include "core/sequencing/AudioClip.h"
#include "core/sequencing/Automation.h"
#include "core/sequencing/AutomationPlayback.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/Metering.h"
#include "core/sequencing/MixerMath.h"
#include "core/sequencing/MixerStrip.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Routing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

TickDuration beats (int count)
{
    return TickDuration::fromTicks (static_cast<std::int64_t> (count) * ticksPerQuarterNote);
}

TickPosition beat (int zeroBasedBeat)
{
    return TickPosition::fromTicks (static_cast<std::int64_t> (zeroBasedBeat) * ticksPerQuarterNote);
}

PluginReference plugin (std::string name = "Device")
{
    return PluginReference {
        std::move (name),
        "TheorySequencer",
        "VST3",
        "/Library/Audio/Plug-Ins/VST3/Device.vst3",
        "device-vst3",
        1234,
        0,
        0,
        2
    };
}

DeviceSlot instrument (std::string id = "instrument")
{
    return DeviceSlot { DeviceSlotId { std::move (id) }, plugin ("Instrument"), PluginKind::instrument };
}

DeviceSlot effect (std::string id = "effect")
{
    return DeviceSlot { DeviceSlotId { std::move (id) }, plugin ("Effect"), PluginKind::audioEffect };
}

AudioSourceReference audioSource()
{
    return AudioSourceReference {
        "audio-source-1",
        "/tmp/audio.wav",
        "Audio",
        false
    };
}
}

TEST_CASE ("Track defaults remain MIDI compatible")
{
    const Track track { "track-1", "Piano" };

    CHECK (track.type() == TrackType::midi);
    CHECK (track.mixerStrip().volumeDb() == 0.0);
    CHECK (track.mixerStrip().pan() == 0.0);
    CHECK (track.routing().audioTo() == RouteEndpoint::master());
    CHECK (track.clips().empty());
    CHECK (track.audioClips().empty());
}

TEST_CASE ("Project enforces a single master track")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "Piano" });
    project.addTrack (Track { "master-1", "Master", TrackType::master });

    REQUIRE (project.masterTrack() != nullptr);
    CHECK (project.masterTrack()->id() == "master-1");
    CHECK_THROWS_AS (project.addTrack (Track { "master-2", "Second Master", TrackType::master }), std::invalid_argument);
}

TEST_CASE ("Track clip ownership follows track type")
{
    Track midiTrack { "midi-1", "MIDI" };
    Track audioTrack { "audio-1", "Audio", TrackType::audio };

    AudioClip clip { "clip-1", "Audio Clip", audioSource(), beat (0), beats (4) };
    CHECK_THROWS_AS (midiTrack.addAudioClip (clip), std::invalid_argument);

    audioTrack.addAudioClip (clip);
    CHECK (audioTrack.audioClips().size() == 1);
    CHECK_THROWS_AS (audioTrack.addClip (MidiClip { "midi-clip-1", "MIDI Clip", beat (0), beats (4) }), std::invalid_argument);
}

TEST_CASE ("Mixer strip clamps volume and pan with useful gain helpers")
{
    MixerStrip strip;
    strip.setVolumeDb (12.0);
    CHECK (strip.volumeDb() == MixerStrip::maximumVolumeDb);

    strip.setVolumeDb (-100.0);
    CHECK (strip.volumeDb() == MixerStrip::minimumFiniteVolumeDb);

    strip.setVolumeDb (MixerStrip::silenceDb());
    CHECK (strip.linearGain() == 0.0);

    strip.setPan (2.0);
    CHECK (strip.pan() == 1.0);

    strip.setMuted (true);
    CHECK (strip.muted());
    CHECK_FALSE (strip.active());

    strip.setMuted (false);
    CHECK_FALSE (strip.muted());
    CHECK (strip.active());

    CHECK (MixerStrip::gainFromDecibels (6.0) == Catch::Approx (1.995262));
    CHECK (MixerStrip::decibelsFromGain (0.0) == MixerStrip::silenceDb());
}

TEST_CASE ("Mixer math parses formats and clamps decibel values")
{
    CHECK (parseDecibelText (" -12.5 dB ").value() == Catch::Approx (-12.5));
    CHECK (parseDecibelText ("off").value() == silenceDecibels());
    CHECK (parseDecibelText ("-inf dB").value() == silenceDecibels());
    CHECK_FALSE (parseDecibelText ("loud").has_value());
    CHECK (parseDecibelText ("24 dB").value() == 6.0);
    CHECK (formatDecibelText (-3.25, 1) == "-3.2 dB");
    CHECK (formatDecibelText (silenceDecibels()) == "-inf dB");
    CHECK (gainFromDecibels (-6.0) == Catch::Approx (0.501187).epsilon (0.0001));
    CHECK (decibelsFromGain (0.5) == Catch::Approx (-6.0206).epsilon (0.0001));
}

TEST_CASE ("Pan law helpers provide predictable stereo gains")
{
    const auto hardLeft = stereoPanGains (-1.0);
    CHECK (hardLeft.left == Catch::Approx (1.0));
    CHECK (hardLeft.right == Catch::Approx (0.0).margin (0.000001));

    const auto centre = stereoPanGains (0.0);
    CHECK (centre.left == Catch::Approx (0.707106).epsilon (0.0001));
    CHECK (centre.right == Catch::Approx (0.707106).epsilon (0.0001));

    const auto linearRight = stereoPanGains (0.25, PanLaw::linearBalance);
    CHECK (linearRight.left == Catch::Approx (0.75));
    CHECK (linearRight.right == Catch::Approx (1.0));
    CHECK (stereoPanGains (2.0).right == Catch::Approx (1.0));
}

TEST_CASE ("Mixer ramps step smoothly to targets")
{
    LinearControlRamp ramp;
    ramp.reset (0.0);
    ramp.setTarget (1.0, 4);

    CHECK (ramp.active());
    CHECK (ramp.next() == Catch::Approx (0.25));
    CHECK (ramp.next() == Catch::Approx (0.5));
    CHECK (ramp.next() == Catch::Approx (0.75));
    CHECK (ramp.next() == Catch::Approx (1.0));
    CHECK_FALSE (ramp.active());
}

TEST_CASE ("Meter ballistics attack release and stopped reset are deterministic")
{
    MeterBallistics meter { MeterBallisticsConfig { -60.0, 10.0, 30.0 } };

    CHECK (meter.valueDb() == -60.0);
    CHECK (meter.process (-12.0, 10.0) == Catch::Approx (-12.0));
    CHECK (meter.process (-60.0, 100.0) == Catch::Approx (-15.0));
    CHECK (meter.process (-60.0, 2000.0) == Catch::Approx (-60.0));

    meter.process (-6.0, 10.0);
    REQUIRE (meter.valueDb() > -10.0);
    meter.reset();
    CHECK (meter.valueDb() == -60.0);
}

TEST_CASE ("Device chain enforces stable IDs and validates track-type rules")
{
    DeviceChain chain;
    chain.appendSlot (instrument());
    chain.appendSlot (effect());

    CHECK (chain.indexOf (DeviceSlotId { "instrument" }) == std::optional<std::size_t> { 0 });
    CHECK (validateDeviceChainForTrackType (TrackType::midi, chain).empty());
    CHECK_THROWS_AS (chain.appendSlot (effect()), std::invalid_argument);

    DeviceChain invalidMidiOrder;
    invalidMidiOrder.appendSlot (effect ("effect-first"));
    invalidMidiOrder.appendSlot (instrument());
    CHECK_FALSE (validateDeviceChainForTrackType (TrackType::midi, invalidMidiOrder).empty());

    DeviceChain invalidAudioChain;
    invalidAudioChain.appendSlot (instrument());
    CHECK_FALSE (validateDeviceChainForTrackType (TrackType::audio, invalidAudioChain).empty());
}

TEST_CASE ("Routing validation catches missing sends and audio cycles")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "One" });
    project.addTrack (Track { "track-2", "Two" });
    project.addTrack (Track { "return-1", "Reverb", TrackType::returnTrack });

    auto* trackOne = project.findTrackById ("track-1");
    auto* trackTwo = project.findTrackById ("track-2");
    REQUIRE (trackOne != nullptr);
    REQUIRE (trackTwo != nullptr);

    auto oneRouting = trackOne->routing();
    oneRouting.setAudioTo (RouteEndpoint::track ("track-2"));
    trackOne->setRouting (oneRouting);

    auto twoRouting = trackTwo->routing();
    twoRouting.setAudioTo (RouteEndpoint::track ("track-1"));
    trackTwo->setRouting (twoRouting);

    auto validation = validateProjectRouting (project);
    CHECK_FALSE (validation.valid());
    CHECK (validation.summary().find ("cycle") != std::string::npos);

    twoRouting.setAudioTo (RouteEndpoint::master());
    trackTwo->setRouting (twoRouting);
    CHECK (validateProjectRouting (project).valid());

    oneRouting.addOrReplaceSend (ReturnSend { "track-2", 0.5 });
    trackOne->setRouting (oneRouting);
    validation = validateProjectRouting (project);
    CHECK_FALSE (validation.valid());
    CHECK (validation.summary().find ("non-return track") != std::string::npos);
}

TEST_CASE ("Routing validation treats sends as feedback graph edges")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "One" });
    project.addTrack (Track { "return-1", "Return", TrackType::returnTrack });
    project.addTrack (Track { "return-2", "Delay", TrackType::returnTrack });

    auto* track = project.findTrackById ("track-1");
    auto* returnOne = project.findTrackById ("return-1");
    auto* returnTwo = project.findTrackById ("return-2");
    REQUIRE (track != nullptr);
    REQUIRE (returnOne != nullptr);
    REQUIRE (returnTwo != nullptr);

    auto trackRouting = track->routing();
    trackRouting.addOrReplaceSend (ReturnSend { "return-1", 0.5 });
    track->setRouting (trackRouting);
    CHECK (validateProjectRouting (project).valid());

    auto returnRouting = returnOne->routing();
    returnRouting.setAudioTo (RouteEndpoint::track ("track-1"));
    returnOne->setRouting (returnRouting);

    auto validation = validateProjectRouting (project);
    CHECK_FALSE (validation.valid());
    CHECK (validation.summary().find ("cycle") != std::string::npos);

    returnRouting.setAudioTo (RouteEndpoint::master());
    returnRouting.addOrReplaceSend (ReturnSend { "return-2", 0.5 });
    returnOne->setRouting (returnRouting);

    auto returnTwoRouting = returnTwo->routing();
    returnTwoRouting.addOrReplaceSend (ReturnSend { "return-1", 0.5 });
    returnTwo->setRouting (returnTwoRouting);

    validation = validateProjectRouting (project);
    CHECK_FALSE (validation.valid());
    CHECK (validation.summary().find ("cycle") != std::string::npos);
}

TEST_CASE ("Routing validation treats audio-from as a graph edge")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "One" });
    project.addTrack (Track { "track-2", "Two", TrackType::audio });

    auto* trackOne = project.findTrackById ("track-1");
    auto* trackTwo = project.findTrackById ("track-2");
    REQUIRE (trackOne != nullptr);
    REQUIRE (trackTwo != nullptr);

    auto oneRouting = trackOne->routing();
    oneRouting.setAudioTo (RouteEndpoint::track ("track-2"));
    trackOne->setRouting (oneRouting);

    auto twoRouting = trackTwo->routing();
    twoRouting.setAudioFrom (RouteEndpoint::track ("track-1"));
    trackTwo->setRouting (twoRouting);
    CHECK (validateProjectRouting (project).valid());

    twoRouting.setAudioTo (RouteEndpoint::track ("track-1"));
    trackTwo->setRouting (twoRouting);

    auto validation = validateProjectRouting (project);
    CHECK_FALSE (validation.valid());
    CHECK (validation.summary().find ("cycle") != std::string::npos);

    twoRouting.setAudioFrom (RouteEndpoint::track ("track-2"));
    twoRouting.setAudioTo (RouteEndpoint::master());
    trackTwo->setRouting (twoRouting);

    validation = validateProjectRouting (project);
    CHECK_FALSE (validation.valid());
    CHECK (validation.summary().find ("cannot receive audio from itself") != std::string::npos);
}

TEST_CASE ("Return tracks fed by soloed sources remain part of the solo path")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "Lead" });
    project.addTrack (Track { "return-1", "Reverb", TrackType::returnTrack });
    project.addTrack (Track { "return-2", "Delay", TrackType::returnTrack });

    auto* track = project.findTrackById ("track-1");
    auto* returnOne = project.findTrackById ("return-1");
    REQUIRE (track != nullptr);
    REQUIRE (returnOne != nullptr);

    auto trackRouting = track->routing();
    trackRouting.addOrReplaceSend (ReturnSend { "return-1", 0.5 });
    track->setRouting (trackRouting);

    auto returnRouting = returnOne->routing();
    returnRouting.addOrReplaceSend (ReturnSend { "return-2", 0.5 });
    returnOne->setRouting (returnRouting);

    CHECK_FALSE (returnTrackIsRequiredForSoloPath (project, "return-1"));
    CHECK_FALSE (returnTrackIsRequiredForSoloPath (project, "return-2"));

    auto strip = track->mixerStrip();
    strip.setSoloed (true);
    track->setMixerStrip (strip);

    CHECK (returnTrackIsRequiredForSoloPath (project, "return-1"));
    CHECK (returnTrackIsRequiredForSoloPath (project, "return-2"));
    CHECK_FALSE (returnTrackIsRequiredForSoloPath (project, "missing-return"));
}

TEST_CASE ("Automation curves validate and query normalized values")
{
    AutomationCurve curve;
    curve.addPoint (AutomationPoint { beat (0), 0.0, AutomationInterpolation::linear });
    curve.addPoint (AutomationPoint { beat (4), 1.0, AutomationInterpolation::hold });
    curve.addPoint (AutomationPoint { beat (8), 0.25 });

    CHECK (curve.valueAt (beat (-1), 0.5) == 0.5);
    CHECK (curve.valueAt (beat (2), 0.5) == Catch::Approx (0.5));
    CHECK (curve.valueAt (beat (6), 0.5) == 1.0);
    CHECK (curve.valueAt (beat (10), 0.5) == 0.25);
    CHECK_THROWS_AS (curve.addPoint (AutomationPoint { beat (4), 0.25 }), std::invalid_argument);
    CHECK_THROWS_AS (AutomationPoint (beat (12), 1.5), std::invalid_argument);
}

TEST_CASE ("Automation targets have stable IDs")
{
    const auto target = AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "device-1" }, "cutoff");
    CHECK (target.isValid());
    CHECK (target.stableId() == "track-1:device:device-1:parameter:cutoff");
}

TEST_CASE ("Automation playback snapshot resolves available targets")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "Piano" });
    project.addTrack (Track { "return-1", "Reverb", TrackType::returnTrack });

    auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);

    MixerStrip strip;
    strip.setVolumeDb (-6.0);
    strip.setPan (-0.25);
    track->setMixerStrip (strip);

    auto routing = track->routing();
    routing.addOrReplaceSend (ReturnSend { "return-1", 0.2 });
    track->setRouting (routing);

    AutomationCurve volume;
    volume.addPoint (AutomationPoint { beat (0), 0.0 });
    volume.addPoint (AutomationPoint { beat (4), 1.0 });
    track->setAutomationLane (AutomationLane { AutomationTarget::trackVolume ("track-1"), volume });

    AutomationCurve pan;
    pan.addPoint (AutomationPoint { beat (0), 0.5, AutomationInterpolation::hold });
    pan.addPoint (AutomationPoint { beat (4), 1.0 });
    track->setAutomationLane (AutomationLane { AutomationTarget::trackPan ("track-1"), pan });

    AutomationCurve send;
    send.addPoint (AutomationPoint { beat (0), 0.7 });
    track->setAutomationLane (AutomationLane { AutomationTarget::sendLevel ("track-1", "return-1"), send });

    track->setAutomationLane (AutomationLane {
        AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "missing-device" }, "cutoff"),
        send
    });

    const auto snapshot = automationPlaybackSnapshotAt (project, beat (2));
    REQUIRE (snapshot.values.size() == 3);

    const auto findValue = [&snapshot] (AutomationTarget target) -> std::optional<double>
    {
        const auto match = std::find_if (snapshot.values.begin(), snapshot.values.end(), [&target] (const auto& value) {
            return value.target == target;
        });

        if (match == snapshot.values.end())
            return std::nullopt;

        return match->normalizedValue;
    };

    REQUIRE (findValue (AutomationTarget::trackVolume ("track-1")).has_value());
    CHECK (*findValue (AutomationTarget::trackVolume ("track-1")) == Catch::Approx (0.5));
    REQUIRE (findValue (AutomationTarget::trackPan ("track-1")).has_value());
    CHECK (*findValue (AutomationTarget::trackPan ("track-1")) == Catch::Approx (0.5));
    REQUIRE (findValue (AutomationTarget::sendLevel ("track-1", "return-1")).has_value());
    CHECK (*findValue (AutomationTarget::sendLevel ("track-1", "return-1")) == Catch::Approx (0.7));
    CHECK_FALSE (findValue (AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "missing-device" }, "cutoff")).has_value());
}
