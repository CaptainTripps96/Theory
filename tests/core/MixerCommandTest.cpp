#include "core/commands/AddTrackCommand.h"
#include "core/commands/CommandStack.h"
#include "core/commands/MixerCommands.h"
#include "core/sequencing/Project.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace
{
using namespace tsq::core::commands;
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

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

DeviceSlot effect (std::string id, std::string name = "Effect")
{
    return DeviceSlot { DeviceSlotId { std::move (id) }, plugin (std::move (name)), PluginKind::audioEffect };
}

Project projectWithMidiTrack()
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "Piano" });
    return project;
}
}

TEST_CASE ("Typed add track command participates in undo and redo")
{
    Project project { "project-1", "Song" };
    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddTrackCommand> ("audio-1", "Audio", TrackType::audio)).succeeded());
    REQUIRE (project.findTrackById ("audio-1") != nullptr);
    CHECK (project.findTrackById ("audio-1")->type() == TrackType::audio);

    REQUIRE (stack.undo().succeeded());
    CHECK (project.findTrackById ("audio-1") == nullptr);

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.findTrackById ("audio-1") != nullptr);
    CHECK (project.findTrackById ("audio-1")->type() == TrackType::audio);
}

TEST_CASE ("Mixer strip command supports undo")
{
    auto project = projectWithMidiTrack();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    MixerStrip strip;
    strip.setVolumeDb (-12.0);
    strip.setPan (0.75);
    strip.setMuted (true);

    REQUIRE (stack.execute (std::make_unique<SetTrackMixerStripCommand> ("track-1", strip)).succeeded());
    CHECK (project.findTrackById ("track-1")->mixerStrip().volumeDb() == -12.0);
    CHECK (project.findTrackById ("track-1")->mixerStrip().pan() == 0.75);
    CHECK (project.findTrackById ("track-1")->mixerStrip().muted());

    REQUIRE (stack.undo().succeeded());
    CHECK (project.findTrackById ("track-1")->mixerStrip().volumeDb() == 0.0);
    CHECK_FALSE (project.findTrackById ("track-1")->mixerStrip().muted());
}

TEST_CASE ("Device commands preserve ordering and undo bypass")
{
    auto project = projectWithMidiTrack();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddTrackDeviceCommand> ("track-1", instrument())).succeeded());
    REQUIRE (stack.execute (std::make_unique<AddTrackDeviceCommand> ("track-1", effect ("effect-a"))).succeeded());
    REQUIRE (stack.execute (std::make_unique<AddTrackDeviceCommand> ("track-1", effect ("effect-b"))).succeeded());

    auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    REQUIRE (track->deviceChain().slots().size() == 3);
    CHECK (track->deviceChain().slots()[2].id() == DeviceSlotId { "effect-b" });

    REQUIRE (stack.execute (std::make_unique<SetTrackDeviceBypassCommand> ("track-1", DeviceSlotId { "effect-b" }, true)).succeeded());
    CHECK (track->deviceChain().findSlot (DeviceSlotId { "effect-b" })->bypassed());

    REQUIRE (stack.undo().succeeded());
    CHECK_FALSE (track->deviceChain().findSlot (DeviceSlotId { "effect-b" })->bypassed());

    REQUIRE (stack.execute (std::make_unique<MoveTrackDeviceCommand> ("track-1", DeviceSlotId { "effect-b" }, 1)).succeeded());
    CHECK (track->deviceChain().slots()[1].id() == DeviceSlotId { "effect-b" });

    REQUIRE (stack.undo().succeeded());
    CHECK (track->deviceChain().slots()[2].id() == DeviceSlotId { "effect-b" });

    REQUIRE (stack.execute (std::make_unique<ReplaceTrackDeviceCommand> ("track-1", effect ("effect-a", "Chorus"))).succeeded());
    REQUIRE (track->deviceChain().findSlot (DeviceSlotId { "effect-a" }) != nullptr);
    CHECK (track->deviceChain().findSlot (DeviceSlotId { "effect-a" })->plugin().pluginName == "Chorus");

    REQUIRE (stack.undo().succeeded());
    CHECK (track->deviceChain().findSlot (DeviceSlotId { "effect-a" })->plugin().pluginName == "Effect");

    REQUIRE (stack.execute (std::make_unique<RemoveTrackDeviceCommand> ("track-1", DeviceSlotId { "effect-b" })).succeeded());
    CHECK (track->deviceChain().findSlot (DeviceSlotId { "effect-b" }) == nullptr);

    REQUIRE (stack.undo().succeeded());
    REQUIRE (track->deviceChain().slots().size() == 3);
    CHECK (track->deviceChain().slots()[2].id() == DeviceSlotId { "effect-b" });

    REQUIRE (stack.redo().succeeded());
    CHECK (track->deviceChain().findSlot (DeviceSlotId { "effect-b" }) == nullptr);
}

TEST_CASE ("Command stack can roll back a just-executed device edit without redo")
{
    auto project = projectWithMidiTrack();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddTrackDeviceCommand> ("track-1", instrument())).succeeded());
    REQUIRE (project.findTrackById ("track-1")->deviceChain().slots().size() == 1);

    REQUIRE (stack.rollbackLastExecuted().succeeded());
    CHECK (project.findTrackById ("track-1")->deviceChain().empty());
    CHECK_FALSE (stack.canUndo());
    CHECK_FALSE (stack.canRedo());
}

TEST_CASE ("Invalid device command does not mutate track chain")
{
    auto project = projectWithMidiTrack();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    const auto result = stack.execute (std::make_unique<AddTrackDeviceCommand> ("track-1", effect ("effect-first")));
    CHECK (result.failed());
    CHECK (project.findTrackById ("track-1")->deviceChain().empty());

    REQUIRE (stack.execute (std::make_unique<AddTrackDeviceCommand> ("track-1", instrument())).succeeded());
    const auto replaceResult = stack.execute (std::make_unique<ReplaceTrackDeviceCommand> ("track-1", effect ("instrument")));
    CHECK (replaceResult.failed());
    REQUIRE (project.findTrackById ("track-1")->deviceChain().slots().size() == 1);
    CHECK (project.findTrackById ("track-1")->deviceChain().slots()[0].kind() == PluginKind::instrument);
}

TEST_CASE ("Device removal invalidates and restores device automation lanes")
{
    auto project = projectWithMidiTrack();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddTrackDeviceCommand> ("track-1", instrument())).succeeded());

    auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);

    AutomationCurve curve;
    curve.addPoint (AutomationPoint { beat (0), 0.25 });
    track->setAutomationLane (AutomationLane { AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "instrument" }, "cutoff"), curve });
    track->setAutomationLane (AutomationLane { AutomationTarget::deviceBypass ("track-1", DeviceSlotId { "instrument" }), curve });
    REQUIRE (track->automationLanes().size() == 2);

    REQUIRE (stack.execute (std::make_unique<RemoveTrackDeviceCommand> ("track-1", DeviceSlotId { "instrument" })).succeeded());
    CHECK (track->deviceChain().empty());
    CHECK (track->automationLanes().empty());

    REQUIRE (stack.undo().succeeded());
    REQUIRE (track->deviceChain().slots().size() == 1);
    CHECK (track->findAutomationLane (AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "instrument" }, "cutoff")) != nullptr);
    CHECK (track->findAutomationLane (AutomationTarget::deviceBypass ("track-1", DeviceSlotId { "instrument" })) != nullptr);
}

TEST_CASE ("Device replacement invalidates only plugin parameter automation")
{
    auto project = projectWithMidiTrack();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddTrackDeviceCommand> ("track-1", instrument())).succeeded());

    auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);

    AutomationCurve curve;
    curve.addPoint (AutomationPoint { beat (0), 0.25 });
    track->setAutomationLane (AutomationLane { AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "instrument" }, "cutoff"), curve });
    track->setAutomationLane (AutomationLane { AutomationTarget::deviceBypass ("track-1", DeviceSlotId { "instrument" }), curve });

    REQUIRE (stack.execute (std::make_unique<ReplaceTrackDeviceCommand> ("track-1", instrument ("instrument"))).succeeded());
    CHECK (track->findAutomationLane (AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "instrument" }, "cutoff")) == nullptr);
    CHECK (track->findAutomationLane (AutomationTarget::deviceBypass ("track-1", DeviceSlotId { "instrument" })) != nullptr);

    REQUIRE (stack.undo().succeeded());
    CHECK (track->findAutomationLane (AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "instrument" }, "cutoff")) != nullptr);
    CHECK (track->findAutomationLane (AutomationTarget::deviceBypass ("track-1", DeviceSlotId { "instrument" })) != nullptr);
}

TEST_CASE ("Routing command rejects cycles and rolls back")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "One" });
    project.addTrack (Track { "track-2", "Two" });

    auto* trackOne = project.findTrackById ("track-1");
    auto* trackTwo = project.findTrackById ("track-2");
    REQUIRE (trackOne != nullptr);
    REQUIRE (trackTwo != nullptr);

    auto routingTwo = trackTwo->routing();
    routingTwo.setAudioTo (RouteEndpoint::track ("track-1"));
    trackTwo->setRouting (routingTwo);

    ProjectCommandContext context { project };
    CommandStack stack { context };

    auto routingOne = trackOne->routing();
    routingOne.setAudioTo (RouteEndpoint::track ("track-2"));

    const auto result = stack.execute (std::make_unique<SetTrackRoutingCommand> ("track-1", routingOne));
    CHECK (result.failed());
    CHECK (trackOne->routing().audioTo() == RouteEndpoint::master());
    CHECK_FALSE (stack.canUndo());
}

TEST_CASE ("Routing command edits return send levels with undo and redo")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "Piano" });
    project.addTrack (Track { "return-1", "Reverb", TrackType::returnTrack });

    ProjectCommandContext context { project };
    CommandStack stack { context };

    auto routing = project.findTrackById ("track-1")->routing();
    routing.addOrReplaceSend (ReturnSend { "return-1", 0.4 });

    REQUIRE (stack.execute (std::make_unique<SetTrackRoutingCommand> ("track-1", routing)).succeeded());
    REQUIRE (project.findTrackById ("track-1")->routing().sends().size() == 1);
    CHECK (project.findTrackById ("track-1")->routing().sends()[0].targetReturnTrackId == "return-1");
    CHECK (project.findTrackById ("track-1")->routing().sends()[0].normalizedLevel == 0.4);

    REQUIRE (stack.undo().succeeded());
    CHECK (project.findTrackById ("track-1")->routing().sends().empty());

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.findTrackById ("track-1")->routing().sends().size() == 1);
    CHECK (project.findTrackById ("track-1")->routing().sends()[0].normalizedLevel == 0.4);
}

TEST_CASE ("Routing command rejects send feedback cycles")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "Piano" });
    project.addTrack (Track { "return-1", "Reverb", TrackType::returnTrack });

    auto* returnTrack = project.findTrackById ("return-1");
    REQUIRE (returnTrack != nullptr);
    auto returnRouting = returnTrack->routing();
    returnRouting.setAudioTo (RouteEndpoint::track ("track-1"));
    returnTrack->setRouting (returnRouting);

    ProjectCommandContext context { project };
    CommandStack stack { context };

    auto routing = project.findTrackById ("track-1")->routing();
    routing.addOrReplaceSend (ReturnSend { "return-1", 0.4 });

    const auto result = stack.execute (std::make_unique<SetTrackRoutingCommand> ("track-1", routing));
    CHECK (result.failed());
    CHECK (project.findTrackById ("track-1")->routing().sends().empty());
    CHECK_FALSE (stack.canUndo());
}

TEST_CASE ("Automation lane command supports undo")
{
    auto project = projectWithMidiTrack();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    AutomationCurve curve;
    curve.addPoint (AutomationPoint { beat (0), 0.25 });
    curve.addPoint (AutomationPoint { beat (4), 0.75 });
    AutomationLane lane { AutomationTarget::trackVolume ("track-1"), curve };

    REQUIRE (stack.execute (std::make_unique<SetTrackAutomationLaneCommand> ("track-1", lane)).succeeded());
    REQUIRE (project.findTrackById ("track-1")->findAutomationLane (AutomationTarget::trackVolume ("track-1")) != nullptr);

    REQUIRE (stack.undo().succeeded());
    CHECK (project.findTrackById ("track-1")->findAutomationLane (AutomationTarget::trackVolume ("track-1")) == nullptr);
}
