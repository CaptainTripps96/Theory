#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "engine/plugins/PluginDescription.h"
#include "ui/BrowserDragPayload.h"
#include "ui/DeviceChainComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <string>

namespace
{
using namespace tsq;

constexpr int deviceCardWidth = 218;
constexpr int arrowWidth = 28;
constexpr int chainPadding = 10;
constexpr int sourceWidth = 104;

core::sequencing::PluginReference pluginReferenceForIndex (int index)
{
    core::sequencing::PluginReference plugin;
    plugin.pluginName = "Probe Device " + std::to_string (index + 1);
    plugin.manufacturer = "Theory";
    plugin.format = "Probe";
    plugin.fileOrIdentifier = "probe-device-" + std::to_string (index + 1);
    plugin.uniqueIdentifier = "probe-device-uid-" + std::to_string (index + 1);
    plugin.uniqueId = index + 1;
    plugin.numInputChannels = 2;
    plugin.numOutputChannels = 2;
    return plugin;
}

engine::plugins::PluginDescription dragPluginDescription()
{
    engine::plugins::PluginDescription plugin;
    plugin.name = "Probe Drop Effect";
    plugin.manufacturer = "Theory";
    plugin.format = "Probe";
    plugin.fileOrIdentifier = "probe-drop-effect";
    plugin.uniqueIdentifier = "probe-drop-effect-uid";
    plugin.stableIdentifier = "probe-drop-effect-stable";
    plugin.isAudioEffect = true;
    plugin.numInputChannels = 2;
    plugin.numOutputChannels = 2;
    return plugin;
}

core::sequencing::Track makeTrackWithDevices (int deviceCount)
{
    auto track = core::sequencing::Track { "track-device-probe", "Device Probe", core::sequencing::TrackType::audio };
    core::sequencing::DeviceChain chain;
    for (int index = 0; index < deviceCount; ++index)
    {
        core::sequencing::DeviceSlot slot {
            core::sequencing::DeviceSlotId { "slot-" + std::to_string (index + 1) },
            pluginReferenceForIndex (index),
            core::sequencing::PluginKind::audioEffect
        };
        slot.setBypassed ((index % 7) == 0);
        if ((index % 5) == 0)
            slot.setPluginStateFile ("state-" + std::to_string (index + 1) + ".bin");

        chain.appendSlot (std::move (slot));
    }

    track.setDeviceChain (std::move (chain));
    return track;
}

void resetProjectTrack (app::AppServices& services, int deviceCount)
{
    auto& project = services.project();
    while (! project.tracks().empty())
        project.removeTrackById (project.tracks().back().id());

    project.addTrack (makeTrackWithDevices (deviceCount));
    services.setSelectedTrack ("track-device-probe");
}

juce::DragAndDropTarget* dragTargetFor (ui::DeviceChainComponent& component)
{
    juce::Viewport* viewport = nullptr;
    for (int index = 0; viewport == nullptr && index < component.getNumChildComponents(); ++index)
        viewport = dynamic_cast<juce::Viewport*> (component.getChildComponent (index));

    if (viewport == nullptr)
        return nullptr;

    return dynamic_cast<juce::DragAndDropTarget*> (viewport->getViewedComponent());
}

void probeDragMoves (ui::DeviceChainComponent& component, int deviceCount)
{
    auto* target = dragTargetFor (component);
    REQUIRE (target != nullptr);

    auto payload = ui::makePluginDragPayload (dragPluginDescription());
    const auto y = 48;
    const auto firstCardX = chainPadding + sourceWidth + arrowWidth;

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "DeviceChainPerfProbe::drag moves devices=" + std::to_string (deviceCount)
        };

        for (int iteration = 0; iteration < 240; ++iteration)
        {
            const auto index = deviceCount <= 0 ? 0 : iteration % std::max (1, deviceCount);
            const auto x = firstCardX + (index * (deviceCardWidth + arrowWidth)) + (deviceCardWidth / 2);
            target->itemDragMove (juce::DragAndDropTarget::SourceDetails { payload, &component, { x, y } });
        }
    }

    target->itemDragExit (juce::DragAndDropTarget::SourceDetails { payload, &component, { firstCardX, y } });
}

void probeDeviceChain (app::AppServices& services, int deviceCount)
{
    resetProjectTrack (services, deviceCount);

    ui::DeviceChainComponent deviceChain { services };
    deviceChain.setBounds (0, 0, 1280, 132);

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "DeviceChainPerfProbe::refresh cold devices=" + std::to_string (deviceCount)
        };
        deviceChain.refresh();
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "DeviceChainPerfProbe::refresh warm devices=" + std::to_string (deviceCount)
        };
        deviceChain.refresh();
    }

    juce::Image image { juce::Image::ARGB, 1280, 132, true };
    juce::Graphics graphics { image };
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "DeviceChainPerfProbe::paint devices=" + std::to_string (deviceCount)
        };
        deviceChain.paintEntireComponent (graphics, true);
    }

    probeDragMoves (deviceChain, deviceCount);
}
}

TEST_CASE ("Device Chain refresh, paint, and drag preview are performance probed", "[integration][device-chain][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    for (const auto deviceCount : std::array<int, 4> { 0, 8, 32, 96 })
        DYNAMIC_SECTION ("device count " << deviceCount)
        {
            probeDeviceChain (services, deviceCount);
        }
}
