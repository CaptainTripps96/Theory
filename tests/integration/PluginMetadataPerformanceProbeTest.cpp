#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/sequencing/DeviceChain.h"
#include "engine/plugins/PluginDescription.h"
#include "engine/plugins/PluginRegistry.h"
#include "ui/BrowserPanelComponent.h"
#include "ui/PluginBrowserComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
using namespace tsq;

std::filesystem::path uniqueCachePath()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("TheorySequencerPluginMetadataPerf-" + std::to_string (stamp) + ".xml");
}

engine::plugins::PluginParameterDescription fakeParameter (int index)
{
    engine::plugins::PluginParameterDescription parameter;
    parameter.index = index;
    parameter.stableId = "param-" + std::to_string (index);
    parameter.name = "Parameter " + std::to_string (index);
    parameter.label = index % 3 == 0 ? "Oscillator" : (index % 3 == 1 ? "Filter" : "Envelope");
    parameter.units = index % 4 == 0 ? "%" : "";
    parameter.defaultValueNormalized = static_cast<double> (index % 101) / 100.0;
    parameter.minimumValue = 0.0;
    parameter.maximumValue = 1.0;
    parameter.automatable = index % 11 != 0;
    parameter.discrete = index % 17 == 0;
    return parameter;
}

engine::plugins::PluginDescription fakePlugin (int index, int parameterCount)
{
    engine::plugins::PluginDescription plugin;
    plugin.name = "Metadata Probe Plugin " + std::to_string (index);
    plugin.manufacturer = index % 3 == 0 ? "TheorySequencer" : "Synthetic Audio";
    plugin.format = "VST3";
    plugin.category = index % 2 == 0 ? "Instrument|Synth" : "Fx|Dynamics";
    plugin.version = "1." + std::to_string (index % 10);
    plugin.fileOrIdentifier = "/tmp/TheorySequencerPluginMetadataPerf/Metadata Probe Plugin "
        + std::to_string (index) + ".vst3";
    plugin.uniqueIdentifier = "vst3:metadata-probe-plugin-" + std::to_string (index);
    plugin.uniqueId = 700000 + index;
    plugin.deprecatedUid = 0;
    plugin.isInstrument = index % 2 == 0;
    plugin.isAudioEffect = index % 2 != 0;
    plugin.numInputChannels = plugin.isAudioEffect ? 2 : 0;
    plugin.numOutputChannels = 2;
    plugin.parameters.reserve (static_cast<std::size_t> (parameterCount));

    for (auto parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex)
        plugin.parameters.push_back (fakeParameter (parameterIndex));

    plugin.parametersScanned = parameterCount > 0;
    plugin.parameterDiscoveryStatus = parameterCount > 0 ? "synthetic" : "deferred";
    engine::plugins::normalizePluginMetadata (plugin);
    return plugin;
}

std::vector<engine::plugins::PluginDescription> fakePlugins (int count, int parameterCount)
{
    std::vector<engine::plugins::PluginDescription> plugins;
    plugins.reserve (static_cast<std::size_t> (count));

    for (auto index = 0; index < count; ++index)
        plugins.push_back (fakePlugin (index, parameterCount));

    return plugins;
}

std::vector<std::string> stableIdsForStride (int pluginCount, int stride)
{
    std::vector<std::string> stableIds;
    for (auto index = 0; index < pluginCount; index += stride)
        stableIds.push_back ("vst3:metadata-probe-plugin-" + std::to_string (index));

    return stableIds;
}

void probeRegistryLookup (const engine::plugins::PluginRegistry& registry,
                          const std::vector<std::string>& stableIds,
                          int repetitions)
{
    auto found = 0;
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "PluginMetadataPerfProbe::registry stable lookup ids=" + std::to_string (stableIds.size())
                + " repetitions=" + std::to_string (repetitions)
        };

        for (auto repetition = 0; repetition < repetitions; ++repetition)
        {
            for (const auto& stableId : stableIds)
                if (registry.findByStableId (stableId).has_value())
                    ++found;
        }
    }

    CHECK (found == static_cast<int> (stableIds.size()) * repetitions);
}

void probeAutomationParameterTargetBuild (const engine::plugins::PluginRegistry& registry,
                                          const std::vector<std::string>& stableIds)
{
    std::vector<core::sequencing::AutomationTarget> targets;
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "PluginMetadataPerfProbe::automation parameter target build slots=" + std::to_string (stableIds.size())
        };

        auto slotIndex = 0;
        for (const auto& stableId : stableIds)
        {
            const auto plugin = registry.findByStableId (stableId);
            if (! plugin.has_value())
                continue;

            auto addedCount = 0;
            for (const auto& parameter : plugin->parameters)
            {
                if (! parameter.automatable || parameter.stableId.empty())
                    continue;

                targets.push_back (core::sequencing::AutomationTarget::pluginParameter (
                    "track-1",
                    core::sequencing::DeviceSlotId { "slot-" + std::to_string (slotIndex) },
                    parameter.stableId));

                if (++addedCount >= 32)
                    break;
            }

            ++slotIndex;
        }
    }

    CHECK_FALSE (targets.empty());
}
}

TEST_CASE ("Plugin registry, browser, and parameter metadata paths are performance probed",
           "[integration][plugin-metadata][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    constexpr auto pluginCount = 1000;
    constexpr auto parameterCount = 64;
    const auto plugins = fakePlugins (pluginCount, parameterCount);
    const auto cachePath = uniqueCachePath();
    std::filesystem::remove (cachePath);

    engine::plugins::PluginRegistry registry { cachePath.string() };
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "PluginMetadataPerfProbe::registry replaceAll plugins=" + std::to_string (pluginCount)
                + " paramsPerPlugin=" + std::to_string (parameterCount)
        };
        registry.replaceAll (plugins);
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "PluginMetadataPerfProbe::registry save plugins=" + std::to_string (pluginCount)
                + " paramsPerPlugin=" + std::to_string (parameterCount)
        };
        REQUIRE (registry.save());
    }

    engine::plugins::PluginRegistry loadedRegistry { cachePath.string() };
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "PluginMetadataPerfProbe::registry load plugins=" + std::to_string (pluginCount)
                + " paramsPerPlugin=" + std::to_string (parameterCount)
        };
        REQUIRE (loadedRegistry.load());
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer { "PluginMetadataPerfProbe::registry classify snapshots" };
        const auto instruments = loadedRegistry.instruments();
        const auto audioEffects = loadedRegistry.audioEffects();
        CHECK_FALSE (instruments.empty());
        CHECK_FALSE (audioEffects.empty());
    }

    const auto lookupStableIds = stableIdsForStride (pluginCount, 7);
    probeRegistryLookup (loadedRegistry, lookupStableIds, 20);
    probeAutomationParameterTargetBuild (loadedRegistry, stableIdsForStride (pluginCount, 83));

    app::AppServices services;
    ui::BrowserPanelComponent browserPanel { services };
    browserPanel.setBounds (0, 0, 360, 720);
    services.pluginRegistry().replaceAll (plugins);
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "PluginMetadataPerfProbe::BrowserPanel refresh plugins=" + std::to_string (pluginCount)
                + " paramsPerPlugin=" + std::to_string (parameterCount)
        };
        browserPanel.refresh();
    }

    ui::PluginBrowserComponent pluginBrowser { services };
    pluginBrowser.setBounds (0, 0, 900, 720);
    services.pluginRegistry().replaceAll (plugins);
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "PluginMetadataPerfProbe::PluginBrowser refresh plugins=" + std::to_string (pluginCount)
                + " paramsPerPlugin=" + std::to_string (parameterCount)
        };
        pluginBrowser.refresh();
    }

    std::filesystem::remove (cachePath);
    CHECK (loadedRegistry.pluginCount() == pluginCount);
}
