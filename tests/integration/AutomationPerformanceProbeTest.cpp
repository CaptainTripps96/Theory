#include "app/AppServices.h"
#include "core/commands/CommandStack.h"
#include "core/commands/MixerCommands.h"
#include "core/commands/ProjectCommandContext.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/sequencing/Automation.h"
#include "core/sequencing/AutomationPlayback.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Routing.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "core/time/Tick.h"
#include "ui/TimelineComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace
{
using namespace tsq;

core::time::TickPosition beat (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote);
}

core::sequencing::PluginReference probePluginReference()
{
    core::sequencing::PluginReference plugin;
    plugin.pluginName = "Automation Probe";
    plugin.manufacturer = "Theory";
    plugin.format = "Probe";
    plugin.fileOrIdentifier = "automation-probe";
    plugin.uniqueIdentifier = "automation-probe-uid";
    plugin.numInputChannels = 2;
    plugin.numOutputChannels = 2;
    return plugin;
}

core::sequencing::DeviceSlot probeDeviceSlot()
{
    return core::sequencing::DeviceSlot {
        core::sequencing::DeviceSlotId { "slot-1" },
        probePluginReference(),
        core::sequencing::PluginKind::instrument
    };
}

core::sequencing::AutomationCurve automationCurveWithPoints (int pointCount)
{
    core::sequencing::AutomationCurve curve;
    for (auto index = 0; index < pointCount; ++index)
    {
        curve.addPoint (core::sequencing::AutomationPoint {
            beat (index),
            static_cast<double> (index % 101) / 100.0,
            (index % 7) == 0 ? core::sequencing::AutomationInterpolation::hold
                             : core::sequencing::AutomationInterpolation::linear
        });
    }

    return curve;
}

core::sequencing::AutomationTarget automationTargetForLane (const std::string& trackId, int laneIndex)
{
    if (laneIndex == 0)
        return core::sequencing::AutomationTarget::trackVolume (trackId);

    if (laneIndex == 1)
        return core::sequencing::AutomationTarget::trackPan (trackId);

    if (laneIndex == 2)
        return core::sequencing::AutomationTarget::trackMute (trackId);

    if (laneIndex == 3)
        return core::sequencing::AutomationTarget::sendLevel (trackId, "return-1");

    if (laneIndex == 4)
        return core::sequencing::AutomationTarget::deviceBypass (trackId, core::sequencing::DeviceSlotId { "slot-1" });

    return core::sequencing::AutomationTarget::pluginParameter (
        trackId,
        core::sequencing::DeviceSlotId { "slot-1" },
        "parameter-" + std::to_string (laneIndex));
}

core::sequencing::Track automationTrack (int trackIndex, int lanesPerTrack, int pointsPerLane)
{
    const auto trackId = "track-" + std::to_string (trackIndex);
    core::sequencing::Track track {
        trackId,
        "Automation Track " + std::to_string (trackIndex + 1),
        core::sequencing::TrackType::midi
    };

    core::sequencing::DeviceChain chain;
    chain.appendSlot (probeDeviceSlot());
    track.setDeviceChain (std::move (chain));

    auto routing = track.routing();
    routing.addOrReplaceSend (core::sequencing::ReturnSend { "return-1", 0.65 });
    track.setRouting (routing);

    for (auto laneIndex = 0; laneIndex < lanesPerTrack; ++laneIndex)
    {
        auto lane = core::sequencing::AutomationLane {
            automationTargetForLane (trackId, laneIndex),
            automationCurveWithPoints (pointsPerLane)
        };
        lane.setVisible (true);
        track.setAutomationLane (std::move (lane));
    }

    return track;
}

core::sequencing::Project automationProject (int trackCount, int lanesPerTrack, int pointsPerLane)
{
    core::sequencing::Project project {
        "automation-probe-" + std::to_string (trackCount) + "-" + std::to_string (lanesPerTrack) + "-" + std::to_string (pointsPerLane),
        "Automation Probe"
    };

    for (auto trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        project.addTrack (automationTrack (trackIndex, lanesPerTrack, pointsPerLane));

    project.addTrack (core::sequencing::Track { "return-1", "Return 1", core::sequencing::TrackType::returnTrack });
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

void probeCurveBuild()
{
    for (const auto pointCount : std::array<int, 3> { 100, 1000, 5000 })
    {
        core::sequencing::AutomationCurve curve;
        {
            core::diagnostics::ScopedPerformanceTimer timer {
                "AutomationPerfProbe::build curve points=" + std::to_string (pointCount)
            };
            curve = automationCurveWithPoints (pointCount);
        }

        CHECK (static_cast<int> (curve.points().size()) == pointCount);
    }
}

void probeSnapshot (const core::sequencing::Project& project,
                    int totalLanes,
                    int pointsPerLane,
                    const std::string& label)
{
    const auto position = beat (pointsPerLane / 2);
    core::sequencing::AutomationPlaybackSnapshot snapshot;
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AutomationPerfProbe::snapshot " + label
                + " lanes=" + std::to_string (totalLanes)
                + " pointsPerLane=" + std::to_string (pointsPerLane)
        };
        snapshot = core::sequencing::automationPlaybackSnapshotAt (project, position);
    }

    CHECK (static_cast<int> (snapshot.values.size()) == totalLanes);
}

void probeSetAutomationLaneCommand (int pointsPerLane)
{
    auto project = automationProject (1, 1, pointsPerLane);
    core::commands::ProjectCommandContext context { project };
    core::commands::CommandStack stack { context };

    auto lane = core::sequencing::AutomationLane {
        core::sequencing::AutomationTarget::trackVolume ("track-0"),
        automationCurveWithPoints (pointsPerLane)
    };
    lane.setVisible (true);

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AutomationPerfProbe::execute SetTrackAutomationLane points=" + std::to_string (pointsPerLane)
        };
        REQUIRE (stack.execute (std::make_unique<core::commands::SetTrackAutomationLaneCommand> ("track-0", std::move (lane))).succeeded());
    }

    CHECK (project.findTrackById ("track-0")->findAutomationLane (core::sequencing::AutomationTarget::trackVolume ("track-0")) != nullptr);
}

void probeTimelinePaint (int trackCount, int lanesPerTrack, int pointsPerLane)
{
    app::AppServices services;
    replaceProject (services, automationProject (trackCount, lanesPerTrack, pointsPerLane));

    ui::TimelineComponent timeline { services };
    const auto height = 260 + (trackCount * (96 + (lanesPerTrack * 48)));
    timeline.setBounds (0, 0, 1440, height);

    juce::Image image { juce::Image::ARGB, 1440, height, true };
    juce::Graphics graphics { image };

    const auto totalLanes = trackCount * lanesPerTrack;
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "AutomationPerfProbe::timeline paint tracks=" + std::to_string (trackCount)
                + " lanes=" + std::to_string (totalLanes)
                + " pointsPerLane=" + std::to_string (pointsPerLane)
        };
        timeline.paintEntireComponent (graphics, true);
    }
}
}

TEST_CASE ("Automation editing and playback paths are performance probed", "[integration][automation][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    probeCurveBuild();

    {
        const auto project = automationProject (1, 0, 0);
        probeSnapshot (project, 0, 0, "empty");
    }

    {
        const auto project = automationProject (2, 5, 100);
        probeSnapshot (project, 10, 100, "small");
    }

    {
        const auto project = automationProject (10, 10, 100);
        probeSnapshot (project, 100, 100, "many-lanes");
    }

    for (const auto pointCount : std::array<int, 3> { 100, 1000, 5000 })
        probeSetAutomationLaneCommand (pointCount);

    probeTimelinePaint (2, 5, 100);
    probeTimelinePaint (4, 10, 100);
    probeTimelinePaint (1, 10, 1000);
}
