#include "app/AppServices.h"
#include "core/commands/AddNoteCommand.h"
#include "core/commands/MoveNoteCommand.h"
#include "core/commands/ProjectCommandContext.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/music_theory/MidiPitch.h"
#include "core/sequencing/Automation.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/MidiNote.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "engine/plugins/PluginDescription.h"
#include "engine/plugins/PluginRegistry.h"
#include "ui/BrowserPanelComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/TimelineComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace
{
std::atomic<int> allocationProbeDepth { 0 };
std::atomic<std::uint64_t> allocationProbeCount { 0 };
std::atomic<std::uint64_t> allocationProbeBytes { 0 };
thread_local bool allocationProbeInsideAllocator = false;

void recordAllocation (std::size_t size) noexcept
{
    if (allocationProbeDepth.load (std::memory_order_relaxed) <= 0 || allocationProbeInsideAllocator)
        return;

    allocationProbeInsideAllocator = true;
    allocationProbeCount.fetch_add (1, std::memory_order_relaxed);
    allocationProbeBytes.fetch_add (static_cast<std::uint64_t> (size), std::memory_order_relaxed);
    allocationProbeInsideAllocator = false;
}

void* allocateProbeMemory (std::size_t size, std::size_t alignment)
{
    const auto safeSize = std::max<std::size_t> (1, size);
    recordAllocation (safeSize);

    if (alignment <= alignof (std::max_align_t))
    {
        if (auto* memory = std::malloc (safeSize))
            return memory;

        throw std::bad_alloc {};
    }

    void* memory = nullptr;
    if (posix_memalign (&memory, alignment, safeSize) == 0 && memory != nullptr)
        return memory;

    throw std::bad_alloc {};
}

void* allocateProbeMemoryNoThrow (std::size_t size, std::size_t alignment) noexcept
{
    try
    {
        return allocateProbeMemory (size, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}

void freeProbeMemory (void* memory) noexcept
{
    std::free (memory);
}
}

void* operator new (std::size_t size)
{
    return allocateProbeMemory (size, alignof (std::max_align_t));
}

void* operator new[] (std::size_t size)
{
    return allocateProbeMemory (size, alignof (std::max_align_t));
}

void* operator new (std::size_t size, std::align_val_t alignment)
{
    return allocateProbeMemory (size, static_cast<std::size_t> (alignment));
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return allocateProbeMemory (size, static_cast<std::size_t> (alignment));
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    return allocateProbeMemoryNoThrow (size, alignof (std::max_align_t));
}

void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    return allocateProbeMemoryNoThrow (size, alignof (std::max_align_t));
}

void* operator new (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateProbeMemoryNoThrow (size, static_cast<std::size_t> (alignment));
}

void* operator new[] (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateProbeMemoryNoThrow (size, static_cast<std::size_t> (alignment));
}

void operator delete (void* memory) noexcept
{
    freeProbeMemory (memory);
}

void operator delete[] (void* memory) noexcept
{
    freeProbeMemory (memory);
}

void operator delete (void* memory, std::size_t) noexcept
{
    freeProbeMemory (memory);
}

void operator delete[] (void* memory, std::size_t) noexcept
{
    freeProbeMemory (memory);
}

void operator delete (void* memory, std::align_val_t) noexcept
{
    freeProbeMemory (memory);
}

void operator delete[] (void* memory, std::align_val_t) noexcept
{
    freeProbeMemory (memory);
}

void operator delete (void* memory, std::size_t, std::align_val_t) noexcept
{
    freeProbeMemory (memory);
}

void operator delete[] (void* memory, std::size_t, std::align_val_t) noexcept
{
    freeProbeMemory (memory);
}

void operator delete (void* memory, const std::nothrow_t&) noexcept
{
    freeProbeMemory (memory);
}

void operator delete[] (void* memory, const std::nothrow_t&) noexcept
{
    freeProbeMemory (memory);
}

void operator delete (void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    freeProbeMemory (memory);
}

void operator delete[] (void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    freeProbeMemory (memory);
}

namespace
{
using namespace tsq;

class ScopedAllocationProbe final
{
public:
    explicit ScopedAllocationProbe (const char* label)
        : label_ (label),
          startCount_ (allocationProbeCount.load (std::memory_order_relaxed)),
          startBytes_ (allocationProbeBytes.load (std::memory_order_relaxed))
    {
        allocationProbeDepth.fetch_add (1, std::memory_order_relaxed);
    }

    ~ScopedAllocationProbe()
    {
        allocationProbeDepth.fetch_sub (1, std::memory_order_relaxed);
        const auto count = allocationProbeCount.load (std::memory_order_relaxed) - startCount_;
        const auto bytes = allocationProbeBytes.load (std::memory_order_relaxed) - startBytes_;
        core::diagnostics::writePerformanceTrace (
            std::string { "MemoryAllocationPerfProbe::" } + label_
                + " allocations=" + std::to_string (count)
                + " bytes=" + std::to_string (bytes),
            0);
    }

private:
    const char* label_ = "";
    std::uint64_t startCount_ = 0;
    std::uint64_t startBytes_ = 0;
};

core::time::TickPosition beat (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote);
}

core::time::TickDuration beats (std::int64_t value)
{
    return core::time::TickDuration::fromTicks (value * core::time::ticksPerQuarterNote);
}

core::time::TickPosition sixteenth (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote / 4);
}

core::sequencing::MidiNote note (std::string id, int pitch, core::time::TickPosition start)
{
    return core::sequencing::MidiNote {
        std::move (id),
        core::music_theory::MidiPitch::fromValue (std::clamp (pitch, 36, 96)),
        start,
        core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 2),
        96
    };
}

core::sequencing::MidiClip denseClip (std::string id,
                                      std::int64_t startBeat,
                                      std::int64_t lengthBeats,
                                      int noteCount)
{
    core::sequencing::MidiClip clip { id, id, beat (startBeat), beats (lengthBeats) };
    const auto usableSlots = std::max<std::int64_t> (1, lengthBeats * 4 - 4);
    if (noteCount > 0)
        clip.reserveNotes (static_cast<std::size_t> (noteCount) + std::max<std::size_t> (8, static_cast<std::size_t> (noteCount) / 8));

    for (auto index = 0; index < noteCount; ++index)
    {
        clip.addNote (note ("note-" + std::to_string (index + 1),
                            48 + ((index * 5) % 36),
                            sixteenth (index % usableSlots)));
    }

    return clip;
}

void replaceProject (app::AppServices& services, core::sequencing::Project source)
{
    auto& project = services.project();
    while (! project.tracks().empty())
        project.removeTrackById (project.tracks().back().id());

    while (! source.tracks().empty())
    {
        const auto trackId = source.tracks().front().id();
        project.addTrack (source.removeTrackById (trackId));
    }

    if (! project.tracks().empty())
        services.setSelectedTrack (project.tracks().front().id());
}

core::sequencing::Project allocationProject()
{
    core::sequencing::Project project { "allocation-probe", "Allocation Probe" };

    for (auto trackIndex = 0; trackIndex < 16; ++trackIndex)
    {
        core::sequencing::Track track {
            "track-" + std::to_string (trackIndex + 1),
            "Track " + std::to_string (trackIndex + 1),
            core::sequencing::TrackType::midi
        };

        for (auto clipIndex = 0; clipIndex < 8; ++clipIndex)
        {
            const auto notes = trackIndex == 0 && clipIndex == 0 ? 2000 : 64;
            track.addClip (denseClip ("clip-" + std::to_string (trackIndex + 1) + "-" + std::to_string (clipIndex + 1),
                                      clipIndex * 8,
                                      8,
                                      notes));
        }

        core::sequencing::AutomationCurve curve;
        for (auto pointIndex = 0; pointIndex < 256; ++pointIndex)
            curve.addPoint (core::sequencing::AutomationPoint {
                sixteenth (pointIndex),
                static_cast<double> (pointIndex % 101) / 100.0
            });

        track.setAutomationLane (core::sequencing::AutomationLane {
            core::sequencing::AutomationTarget::trackVolume (track.id()),
            std::move (curve)
        });

        project.addTrack (std::move (track));
    }

    return project;
}

core::sequencing::Project singleDenseClipProject()
{
    core::sequencing::Project project { "allocation-command-probe", "Allocation Command Probe" };
    core::sequencing::Track track { "track-1", "Track 1", core::sequencing::TrackType::midi };
    track.addClip (denseClip ("clip-1-1", 0, 8, 2000));
    project.addTrack (std::move (track));
    return project;
}

engine::plugins::PluginDescription fakePlugin (int index)
{
    engine::plugins::PluginDescription plugin;
    plugin.name = "Allocation Probe Plugin " + std::to_string (index);
    plugin.manufacturer = index % 3 == 0 ? "TheorySequencer" : "Synthetic Audio";
    plugin.format = "VST3";
    plugin.category = index % 2 == 0 ? "Instrument|Synth" : "Fx|Delay";
    plugin.fileOrIdentifier = "/tmp/TheorySequencerAllocationProbe/Plugin " + std::to_string (index) + ".vst3";
    plugin.uniqueIdentifier = "vst3:allocation-probe-plugin-" + std::to_string (index);
    plugin.uniqueId = 900000 + index;
    plugin.isInstrument = index % 2 == 0;
    plugin.isAudioEffect = index % 2 != 0;
    plugin.numInputChannels = plugin.isAudioEffect ? 2 : 0;
    plugin.numOutputChannels = 2;

    for (auto parameterIndex = 0; parameterIndex < 16; ++parameterIndex)
    {
        engine::plugins::PluginParameterDescription parameter;
        parameter.index = parameterIndex;
        parameter.stableId = "param-" + std::to_string (parameterIndex);
        parameter.name = "Parameter " + std::to_string (parameterIndex);
        parameter.defaultValueNormalized = 0.5;
        plugin.parameters.push_back (std::move (parameter));
    }

    plugin.parametersScanned = true;
    plugin.parameterDiscoveryStatus = "synthetic";
    engine::plugins::normalizePluginMetadata (plugin);
    return plugin;
}

std::vector<engine::plugins::PluginDescription> fakePlugins (int count)
{
    std::vector<engine::plugins::PluginDescription> plugins;
    plugins.reserve (static_cast<std::size_t> (count));
    for (auto index = 0; index < count; ++index)
        plugins.push_back (fakePlugin (index));

    return plugins;
}

void probePianoRollPaintAllocations (app::AppServices& services)
{
    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1-1");

    juce::Image image { juce::Image::ARGB, 1280, 720, true };
    juce::Graphics graphics { image };
    pianoRoll.paintEntireComponent (graphics, true);

    {
        ScopedAllocationProbe allocations { "piano-roll warm paint notes=2000" };
        pianoRoll.paintEntireComponent (graphics, true);
    }

    CHECK (pianoRoll.hasOpenClip());
}

void probeTimelinePaintAllocations (app::AppServices& services)
{
    ui::TimelineComponent timeline { services };
    timeline.setBounds (0, 0, 1600, 1720);

    juce::Image image { juce::Image::ARGB, 1600, 1720, true };
    juce::Graphics graphics { image };
    timeline.paintEntireComponent (graphics, true);

    {
        ScopedAllocationProbe allocations { "timeline warm paint tracks=16 clips=128" };
        timeline.paintEntireComponent (graphics, true);
    }
}

void probeCommandAllocations (app::AppServices& services)
{
    auto directClip = denseClip ("direct-clip", 0, 8, 2000);
    {
        ScopedAllocationProbe allocations { "midi-clip direct ten note inserts notes=2000" };
        for (auto index = 0; index < 10; ++index)
            directClip.addNote (note ("direct-added-" + std::to_string (index + 1), 72, sixteenth (index + 1)));
    }

    auto directCommandProject = singleDenseClipProject();
    core::commands::ProjectCommandContext directContext { directCommandProject };
    {
        core::commands::AddNoteCommand warmup {
            "track-1",
            "clip-1-1",
            note ("direct-command-warmup", 72, sixteenth (0))
        };
        REQUIRE (warmup.execute (directContext).succeeded());
    }

    {
        ScopedAllocationProbe allocations { "command execute direct context ten note edits" };
        for (auto index = 0; index < 10; ++index)
        {
            core::commands::AddNoteCommand command {
                "track-1",
                "clip-1-1",
                note ("direct-command-added-" + std::to_string (index + 1), 72, sixteenth (index + 1))
            };
            REQUIRE (command.execute (directContext).succeeded());
        }
    }

    auto addIndex = 0;
    {
        ScopedAllocationProbe allocations { "command execute ten note edits" };
        for (; addIndex < 10; ++addIndex)
        {
            const auto result = services.commandStack().execute (
                std::make_unique<core::commands::AddNoteCommand> (
                    "track-1",
                    "clip-1-1",
                    note ("allocation-added-" + std::to_string (addIndex + 1), 72, sixteenth (addIndex + 1))));
            REQUIRE (result.succeeded());
        }
    }

    {
        ScopedAllocationProbe allocations { "command move ten notes" };
        for (auto index = 0; index < 10; ++index)
        {
            const auto result = services.commandStack().execute (
                std::make_unique<core::commands::MoveNoteCommand> (
                    "track-1",
                    "clip-1-1",
                    "allocation-added-" + std::to_string (index + 1),
                    sixteenth (index + 16)));
            REQUIRE (result.succeeded());
        }
    }
}

void probeBrowserAllocations (app::AppServices& services)
{
    services.pluginRegistry().replaceAll (fakePlugins (2000));

    ui::BrowserPanelComponent browser { services };
    browser.setBounds (0, 0, 360, 720);
    browser.refresh();

    {
        ScopedAllocationProbe allocations { "browser warm refresh plugins=2000" };
        browser.refresh();
    }
}

void probePlaybackStartAllocations (app::AppServices& services)
{
    REQUIRE (services.syncPlaybackProjectIfNeeded());
    services.stopProjectPlayback();

    {
        ScopedAllocationProbe allocations { "playback start synced project" };
        REQUIRE (services.startProjectPlayback());
    }

    services.stopProjectPlayback();
}
}

TEST_CASE ("Memory allocation pressure is probed across broad UI and app paths",
           "[integration][memory][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    replaceProject (services, allocationProject());

    probePianoRollPaintAllocations (services);
    probeTimelinePaintAllocations (services);
    probeCommandAllocations (services);
    probeBrowserAllocations (services);
    probePlaybackStartAllocations (services);
}
