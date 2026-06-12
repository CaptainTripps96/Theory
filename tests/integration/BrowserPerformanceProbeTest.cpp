#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "engine/plugins/PluginDescription.h"
#include "engine/plugins/PluginRegistry.h"
#include "ui/BrowserPanelComponent.h"
#include "ui/PluginBrowserComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
using namespace tsq;

std::filesystem::path uniquePackagePath()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("TheorySequencerBrowserPerfProbe-" + std::to_string (stamp) + ".tseq");
}

engine::plugins::PluginDescription fakePlugin (int index)
{
    engine::plugins::PluginDescription plugin;
    plugin.name = "Probe Plugin " + std::to_string (index);
    plugin.manufacturer = index % 3 == 0 ? "TheorySequencer" : "Synthetic Audio";
    plugin.format = "VST3";
    plugin.category = index % 2 == 0 ? "Instrument|Synth" : "Fx|Delay";
    plugin.fileOrIdentifier = "/tmp/TheorySequencerBrowserPerfProbe/Probe Plugin " + std::to_string (index) + ".vst3";
    plugin.uniqueIdentifier = "vst3:probe-plugin-" + std::to_string (index);
    plugin.uniqueId = 100000 + index;
    plugin.isInstrument = index % 2 == 0;
    plugin.isAudioEffect = index % 2 != 0;
    plugin.numInputChannels = plugin.isAudioEffect ? 2 : 0;
    plugin.numOutputChannels = 2;

    if (index % 5 == 0)
    {
        engine::plugins::PluginParameterDescription parameter;
        parameter.index = 0;
        parameter.stableId = "mix";
        parameter.name = "Mix";
        parameter.defaultValueNormalized = 0.5;
        plugin.parameters.push_back (parameter);
        plugin.parametersScanned = true;
        plugin.parameterDiscoveryStatus = "synthetic";
    }

    engine::plugins::normalizePluginMetadata (plugin);
    return plugin;
}

std::vector<engine::plugins::PluginDescription> fakePlugins (int count)
{
    std::vector<engine::plugins::PluginDescription> plugins;
    plugins.reserve (static_cast<std::size_t> (count));

    for (int index = 0; index < count; ++index)
        plugins.push_back (fakePlugin (index));

    return plugins;
}

void writeSmallFile (const std::filesystem::path& path)
{
    std::filesystem::create_directories (path.parent_path());
    std::ofstream stream { path, std::ios::binary };
    stream << "probe";
}

void addSyntheticPackageFiles (const std::filesystem::path& packagePath, int count)
{
    for (int index = 0; index < count; ++index)
    {
        const auto extension = index % 3 == 0 ? ".wav" : (index % 3 == 1 ? ".mid" : ".flac");
        writeSmallFile (packagePath / "assets" / ("browser-probe-" + std::to_string (index) + extension));
    }
}
}

TEST_CASE ("Browser panel refreshes large registries and packages for performance probing", "[integration][browser][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    services.pluginRegistry().replaceAll (fakePlugins (2000));

    ui::BrowserPanelComponent browserPanel { services };
    browserPanel.setBounds (0, 0, 360, 720);

    {
        core::diagnostics::ScopedPerformanceTimer timer { "BrowserPerfProbe::BrowserPanel refresh no-package plugins=2000" };
        browserPanel.refresh();
    }

    const auto packagePath = uniquePackagePath();
    std::filesystem::remove_all (packagePath);
    REQUIRE (services.saveProjectAs (packagePath));
    addSyntheticPackageFiles (packagePath, 620);

    {
        core::diagnostics::ScopedPerformanceTimer timer { "BrowserPerfProbe::BrowserPanel refresh package-files=620 plugins=2000" };
        browserPanel.refresh();
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer { "BrowserPerfProbe::BrowserPanel refresh package-cache-hit files=620 plugins=2000" };
        browserPanel.refresh();
    }

    ui::PluginBrowserComponent pluginBrowser { services };
    pluginBrowser.setBounds (0, 0, 900, 720);

    {
        core::diagnostics::ScopedPerformanceTimer timer { "BrowserPerfProbe::PluginBrowser refresh plugins=2000" };
        pluginBrowser.refresh();
    }

    std::filesystem::remove_all (packagePath);
    CHECK (services.pluginRegistry().pluginCount() == 2000);
}
