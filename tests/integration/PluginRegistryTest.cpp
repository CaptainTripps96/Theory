#include "engine/plugins/PluginDescription.h"
#include "engine/plugins/PluginRegistry.h"
#include "engine/plugins/PluginScanService.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace
{
using namespace tsq::engine::plugins;

std::filesystem::path uniqueCachePath()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("TheorySequencerPluginRegistryTest-" + std::to_string (stamp) + ".xml");
}

PluginDescription instrumentPlugin()
{
    PluginDescription plugin;
    plugin.name = "Test Synth";
    plugin.manufacturer = "TheorySequencer";
    plugin.format = "VST3";
    plugin.category = "Instrument|Synth";
    plugin.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/Test Synth.vst3";
    plugin.uniqueIdentifier = "vst3:test-synth";
    plugin.uniqueId = 1001;
    plugin.isInstrument = true;
    plugin.numInputChannels = 0;
    plugin.numOutputChannels = 2;
    normalizePluginMetadata (plugin);
    return plugin;
}

PluginDescription effectPlugin()
{
    PluginDescription plugin;
    plugin.name = "Test Delay";
    plugin.manufacturer = "TheorySequencer";
    plugin.format = "VST3";
    plugin.category = "Fx|Delay";
    plugin.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/Test Delay.vst3";
    plugin.uniqueIdentifier = "vst3:test-delay";
    plugin.uniqueId = 2002;
    plugin.isInstrument = false;
    plugin.numInputChannels = 2;
    plugin.numOutputChannels = 2;

    PluginParameterDescription mix;
    mix.index = 0;
    mix.stableId = "mix";
    mix.name = "Mix";
    mix.units = "%";
    mix.defaultValueNormalized = 0.5;
    plugin.parameters = { mix };
    plugin.parametersScanned = true;
    plugin.parameterDiscoveryStatus = "complete";

    normalizePluginMetadata (plugin);
    return plugin;
}
}

TEST_CASE ("Plugin registry migrates schema v1 cache into capability metadata", "[plugin-registry]")
{
    const auto cachePath = uniqueCachePath();
    std::filesystem::remove (cachePath);

    {
        std::ofstream stream { cachePath };
        stream << R"(<?xml version="1.0" encoding="UTF-8"?>
<TheorySequencerPluginRegistry schemaVersion="1">
  <Plugin name="Legacy Synth" manufacturer="TheorySequencer" format="VST3" category="Instrument" version="1.0" fileOrIdentifier="/tmp/Legacy Synth.vst3" uniqueIdentifier="legacy-synth" uniqueId="101" deprecatedUid="0" isInstrument="1" numInputChannels="0" numOutputChannels="2"/>
  <Plugin name="Legacy Compressor" manufacturer="TheorySequencer" format="VST3" category="Fx" version="1.0" fileOrIdentifier="/tmp/Legacy Compressor.vst3" uniqueIdentifier="legacy-compressor" uniqueId="202" deprecatedUid="0" isInstrument="0" numInputChannels="2" numOutputChannels="2"/>
</TheorySequencerPluginRegistry>)";
    }

    PluginRegistry registry { cachePath.string() };
    REQUIRE (registry.load());
    CHECK (registry.pluginCount() == 2);
    CHECK (registry.instruments().size() == 1);
    CHECK (registry.audioEffects().size() == 1);

    const auto effect = registry.findByStableId ("legacy-compressor");
    REQUIRE (effect.has_value());
    CHECK (effect->isAudioEffect);
    CHECK (classifyPlugin (*effect) == PluginCapability::audioEffect);

    REQUIRE (registry.save());
    PluginRegistry reloaded { cachePath.string() };
    REQUIRE (reloaded.load());
    CHECK (reloaded.findByStableId ("legacy-synth").has_value());
    CHECK (reloaded.findByStableId ("legacy-compressor")->isAudioEffect);

    std::filesystem::remove (cachePath);
}

TEST_CASE ("Plugin registry round trips parameters and stable lookup", "[plugin-registry]")
{
    const auto cachePath = uniqueCachePath();
    std::filesystem::remove (cachePath);

    PluginRegistry registry { cachePath.string() };
    registry.replaceAll ({ effectPlugin(), instrumentPlugin() });
    REQUIRE (registry.save());

    PluginRegistry loaded { cachePath.string() };
    REQUIRE (loaded.load());

    const auto effect = loaded.findByStableId ("vst3:test-delay");
    REQUIRE (effect.has_value());
    CHECK (effect->isAudioEffect);
    REQUIRE (effect->parameters.size() == 1);
    CHECK (effect->parameters[0].stableId == "mix");
    CHECK (effect->parameters[0].defaultValueNormalized == 0.5);
    CHECK (effect->parameterDiscoveryStatus == "complete");

    const auto instrument = loaded.findByStableId ("vst3:test-synth");
    REQUIRE (instrument.has_value());
    CHECK (instrument->isInstrument);
    CHECK (classifyPlugin (*instrument) == PluginCapability::instrument);

    std::filesystem::remove (cachePath);
}

TEST_CASE ("Plugin metadata classifies ambiguous and unsupported plugins", "[plugin-registry]")
{
    auto ambiguous = instrumentPlugin();
    ambiguous.numInputChannels = 2;
    normalizePluginMetadata (ambiguous);
    CHECK (ambiguous.isInstrument);
    CHECK (ambiguous.isAudioEffect);
    CHECK (ambiguous.hasAmbiguousCapabilities);
    CHECK (classifyPlugin (ambiguous) == PluginCapability::ambiguous);

    PluginDescription unsupported;
    unsupported.name = "Metadata Only";
    unsupported.format = "VST3";
    normalizePluginMetadata (unsupported);
    CHECK (unsupported.unsupported);
    CHECK (classifyPlugin (unsupported) == PluginCapability::unsupported);
}

TEST_CASE ("Plugin scan status carries structured diagnostics", "[plugin-registry]")
{
    PluginScanStatus status;
    status.completed = true;
    status.failed = true;
    status.scanFailures = 1;
    status.unsupportedPluginsFound = 1;
    status.message = "VST3 scan failed: scanner crashed";
    status.diagnostics.push_back (status.message);

    CHECK (status.failed);
    CHECK (status.scanFailures == 1);
    REQUIRE (status.diagnostics.size() == 1);
    CHECK (status.diagnostics[0].find ("VST3 scan failed") != std::string::npos);
}
