#include "core/devices/FirstPartyDeviceRegistry.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace tsq::core::devices;
using namespace tsq::core::sequencing;

DeviceSlot slotFor (const FirstPartyDeviceDefinition& definition, std::string id)
{
    return DeviceSlot {
        DeviceSlotId { std::move (id) },
        defaultFirstPartyDeviceState (definition),
        definition.kind
    };
}

bool containsTypeId (const std::vector<FirstPartyDeviceDefinition>& definitions, const char* typeId)
{
    return std::any_of (definitions.begin(), definitions.end(), [typeId] (const auto& definition)
    {
        return definition.typeId == typeId;
    });
}
}

TEST_CASE ("First-party device registry exposes native instruments and audio effects", "[devices][first-party]")
{
    const auto definitions = firstPartyDeviceDefinitions();
    CHECK (containsTypeId (definitions, simpleOscComplexTypeId()));
    CHECK (containsTypeId (definitions, nativePhaserTypeId()));
    CHECK (containsTypeId (definitions, nativeReverbTypeId()));
    CHECK (containsTypeId (definitions, nativeTapeSimulatorTypeId()));

    CHECK (nativePhaserDefinition().kind == PluginKind::audioEffect);
    CHECK (nativeReverbDefinition().kind == PluginKind::audioEffect);
    CHECK (nativeTapeSimulatorDefinition().kind == PluginKind::audioEffect);

    CHECK (defaultFirstPartyDeviceState (nativePhaserDefinition()).isValid());
    CHECK (defaultFirstPartyDeviceState (nativeReverbDefinition()).isValid());
    CHECK (defaultFirstPartyDeviceState (nativeTapeSimulatorDefinition()).isValid());

    CHECK (expressionTargetParameters (nativePhaserDefinition()).empty());
    CHECK (expressionTargetParameters (nativeReverbDefinition()).empty());
    CHECK (expressionTargetParameters (nativeTapeSimulatorDefinition()).empty());
}

TEST_CASE ("Native audio effects validate in audio-capable first-party device chains", "[devices][first-party]")
{
    DeviceChain audioChain;
    audioChain.appendSlot (slotFor (nativeReverbDefinition(), "reverb"));
    audioChain.appendSlot (slotFor (nativeTapeSimulatorDefinition(), "tape"));
    CHECK (validateDeviceChainForTrackType (TrackType::audio, audioChain).empty());
    CHECK (validateDeviceChainForTrackType (TrackType::master, audioChain).empty());

    DeviceChain midiChain;
    midiChain.appendSlot (slotFor (simpleOscComplexDefinition(), "simple-osc"));
    midiChain.appendSlot (slotFor (nativePhaserDefinition(), "phaser"));
    CHECK (validateDeviceChainForTrackType (TrackType::midi, midiChain).empty());

    DeviceChain invalidMidiOrder;
    invalidMidiOrder.appendSlot (slotFor (nativePhaserDefinition(), "phaser"));
    invalidMidiOrder.appendSlot (slotFor (simpleOscComplexDefinition(), "simple-osc"));
    CHECK_FALSE (validateDeviceChainForTrackType (TrackType::midi, invalidMidiOrder).empty());
}
