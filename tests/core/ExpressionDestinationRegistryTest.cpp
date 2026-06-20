#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/sequencing/ExpressionDestinationRegistry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

namespace
{
using namespace tsq::core::devices;
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

DeviceSlot simpleOscSlot()
{
    const auto& definition = simpleOscComplexDefinition();
    return DeviceSlot {
        DeviceSlotId { "simple-osc-complex" },
        defaultFirstPartyDeviceState (definition),
        definition.kind
    };
}

PluginReference plugin()
{
    return PluginReference {
        "Filter",
        "TheorySequencer",
        "VST3",
        "/tmp/filter.vst3",
        "filter-vst3",
        1001,
        0,
        2,
        2
    };
}

Project projectWithDestinations()
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "return-1", "Reverb", TrackType::returnTrack });

    Track track { "track-1", "Lead" };
    DeviceChain chain;
    chain.appendSlot (simpleOscSlot());
    chain.appendSlot (DeviceSlot { DeviceSlotId { "filter" }, plugin(), PluginKind::audioEffect });
    track.setDeviceChain (std::move (chain));
    track.addClip (MidiClip { "clip-1", "Clip", beat (0), beats (4) });
    project.addTrack (std::move (track));
    return project;
}
}

TEST_CASE ("Expression destination kind IDs are stable")
{
    CHECK (expressionDestinationKindId (ExpressionDestinationKind::trackVolume) == "trackVolume");
    CHECK (expressionDestinationKindId (ExpressionDestinationKind::firstPartyParameter) == "firstPartyParameter");
    CHECK (expressionDestinationKindFromId ("midiCc") == ExpressionDestinationKind::midiCc);
    CHECK_THROWS_AS (expressionDestinationKindFromId ("nope"), std::invalid_argument);
}

TEST_CASE ("Expression destination registry reports stable IDs and metadata")
{
    const auto project = projectWithDestinations();

    const auto firstParty = ExpressionDestination::firstPartyParameter (
        "track-1",
        DeviceSlotId { "simple-osc-complex" },
        "osc.pm.amount");
    const auto metadata = expressionDestinationMetadata (project, firstParty);

    CHECK (metadata.available);
    CHECK (metadata.expressionTarget);
    CHECK (metadata.stableId == "first-party-parameter:track-1:simple-osc-complex:osc.pm.amount");
    CHECK (metadata.displayName == "PM Amount");
    CHECK (metadata.detailText == "Simple Osc Complex");
    CHECK (metadata.defaultOutputMin == Catch::Approx (0.0));
    CHECK (metadata.defaultOutputMax == Catch::Approx (1.0));

    const auto pan = expressionDestinationMetadata (project, ExpressionDestination::trackPan ("track-1"));
    CHECK (pan.displayName == "Track Pan");
    CHECK (pan.defaultOutputMin == Catch::Approx (-1.0));
    CHECK (pan.defaultOutputMax == Catch::Approx (1.0));

    const auto midi = expressionDestinationMetadata (project, ExpressionDestination::midiCc ("track-1", 74));
    CHECK (midi.displayName == "MIDI CC 74 Brightness");
    CHECK (midi.discrete);
    CHECK (midi.defaultOutputMax == Catch::Approx (127.0));
    CHECK_FALSE (midi.playbackMapped);
    CHECK (midi.plainMidiExportMapped);
    CHECK (midi.supportLabel == "Export only");
}

TEST_CASE ("Expression destination registry reports playback and export support clearly")
{
    const auto project = projectWithDestinations();

    const auto volume = expressionDestinationMetadata (project, ExpressionDestination::trackVolume ("track-1"));
    CHECK (volume.playbackMapped);
    CHECK_FALSE (volume.plainMidiExportMapped);
    CHECK (volume.supportLabel == "Playback");

    const auto firstParty = expressionDestinationMetadata (
        project,
        ExpressionDestination::firstPartyParameter ("track-1", DeviceSlotId { "simple-osc-complex" }, "osc.pm.amount"));
    CHECK (firstParty.playbackMapped);
    CHECK_FALSE (firstParty.plainMidiExportMapped);
    CHECK (firstParty.supportLabel == "Playback");

    const auto midi = expressionDestinationMetadata (project, ExpressionDestination::midiCc ("track-1", 11));
    CHECK_FALSE (midi.playbackMapped);
    CHECK (midi.plainMidiExportMapped);
    CHECK (midi.supportLabel == "Export only");

    const auto plugin = expressionDestinationMetadata (
        project,
        ExpressionDestination::pluginParameter ("track-1", DeviceSlotId { "filter" }, "cutoff"));
    CHECK_FALSE (plugin.playbackMapped);
    CHECK_FALSE (plugin.plainMidiExportMapped);
    CHECK (plugin.supportLabel == "Stored only");
}

TEST_CASE ("Expression destination registry validates destination availability")
{
    const auto project = projectWithDestinations();

    CHECK (expressionDestinationIsAvailable (project, ExpressionDestination::trackVolume ("track-1")));
    CHECK (expressionDestinationIsAvailable (project, ExpressionDestination::sendLevel ("track-1", "return-1")));
    CHECK (expressionDestinationIsAvailable (project, ExpressionDestination::pluginParameter ("track-1", DeviceSlotId { "filter" }, "cutoff")));
    CHECK (expressionDestinationIsAvailable (
        project,
        ExpressionDestination::firstPartyParameter ("track-1", DeviceSlotId { "simple-osc-complex" }, "amp.level")));

    CHECK_FALSE (expressionDestinationIsAvailable (project, ExpressionDestination::trackVolume ("missing")));
    CHECK_FALSE (expressionDestinationIsAvailable (project, ExpressionDestination::sendLevel ("track-1", "missing-return")));
    CHECK_FALSE (expressionDestinationIsAvailable (
        project,
        ExpressionDestination::firstPartyParameter ("track-1", DeviceSlotId { "filter" }, "cutoff")));
    CHECK_FALSE (expressionDestinationIsAvailable (
        project,
        ExpressionDestination::pluginParameter ("track-1", DeviceSlotId { "simple-osc-complex" }, "amp.level")));
}

TEST_CASE ("First-party expression target lookup exposes Simple Osc Complex parameters")
{
    const auto slot = simpleOscSlot();
    const auto* amount = findFirstPartyExpressionTarget (slot, "osc.pm.amount");
    REQUIRE (amount != nullptr);
    CHECK (amount->name == "PM Amount");
    CHECK (amount->expressionTarget);

    const auto targets = expressionTargetParameters (simpleOscComplexDefinition());
    CHECK (targets.size() == simpleOscComplexDefinition().parameters.size());
    CHECK (std::any_of (targets.begin(), targets.end(), [] (const auto& parameter) {
        return parameter.id == "wavefolder.amount";
    }));
}

TEST_CASE ("Expression destination registry enumerates track destinations")
{
    const auto project = projectWithDestinations();
    const auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);

    const auto destinations = expressionDestinationMetadataForTrack (project, *track);
    CHECK (std::any_of (destinations.begin(), destinations.end(), [] (const auto& metadata) {
        return metadata.destination.kind == ExpressionDestinationKind::trackVolume;
    }));
    CHECK (std::any_of (destinations.begin(), destinations.end(), [] (const auto& metadata) {
        return metadata.destination.kind == ExpressionDestinationKind::sendLevel
            && metadata.destination.sendTargetTrackId == "return-1";
    }));
    CHECK (std::any_of (destinations.begin(), destinations.end(), [] (const auto& metadata) {
        return metadata.destination.kind == ExpressionDestinationKind::firstPartyParameter
            && metadata.destination.parameterId == "osc.pm.amount";
    }));
    CHECK (std::any_of (destinations.begin(), destinations.end(), [] (const auto& metadata) {
        return metadata.destination.kind == ExpressionDestinationKind::midiCc
            && metadata.destination.midiCcNumber == 74
            && metadata.plainMidiExportMapped
            && metadata.supportLabel == "Export only";
    }));
}

TEST_CASE ("Expression route mapping supports unipolar bipolar and inverted ranges")
{
    const ExpressionRoute unipolar {
        ExpressionRouteId { "route-1" },
        ExpressionDestination::midiCc ("track-1", 74),
        10.0,
        110.0
    };
    CHECK (mapExpressionRouteValue (unipolar, 0.0, ExpressionLanePolarity::unipolar) == Catch::Approx (10.0));
    CHECK (mapExpressionRouteValue (unipolar, 0.5, ExpressionLanePolarity::unipolar) == Catch::Approx (60.0));
    CHECK (mapExpressionRouteValue (unipolar, 1.0, ExpressionLanePolarity::unipolar) == Catch::Approx (110.0));

    const ExpressionRoute inverted {
        ExpressionRouteId { "route-2" },
        ExpressionDestination::trackPan ("track-1"),
        1.0,
        -1.0
    };
    CHECK (mapExpressionRouteValue (inverted, -1.0, ExpressionLanePolarity::bipolar) == Catch::Approx (1.0));
    CHECK (mapExpressionRouteValue (inverted, 0.0, ExpressionLanePolarity::bipolar) == Catch::Approx (0.0));
    CHECK (mapExpressionRouteValue (inverted, 1.0, ExpressionLanePolarity::bipolar) == Catch::Approx (-1.0));
    CHECK (defaultExpressionRouteSmoothingPolicy (inverted.destination()) == ExpressionRouteSmoothingPolicy::none);
}

TEST_CASE ("Expression route equality and serialization-compatible destinations remain stable")
{
    const auto destination = ExpressionDestination::sendLevel ("track-1", "return-1");
    const auto sameDestination = ExpressionDestination::sendLevel ("track-1", "return-1");
    const auto otherDestination = ExpressionDestination::sendLevel ("track-1", "return-2");

    CHECK (destination == sameDestination);
    CHECK (destination != otherDestination);
    CHECK (destination.stableId() == "send-level:track-1:return-1");

    const ExpressionRoute route {
        ExpressionRouteId { "route-1" },
        destination,
        0.2,
        0.8
    };
    CHECK (route.destination() == destination);
    CHECK (route.mapLaneValue (0.5, ExpressionLanePolarity::unipolar) == Catch::Approx (0.5));
}
