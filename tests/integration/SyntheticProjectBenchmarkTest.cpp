#include "../performance/SyntheticProjectFixtures.h"

#include "core/commands/AddNoteCommand.h"
#include "core/commands/ProjectCommandContext.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/midi/MidiExporter.h"
#include "core/midi/MidiImporter.h"
#include "core/serialization/ProjectSerializer.h"
#include "engine/TracktionPlaybackEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <string>
#include <utility>

namespace
{
using namespace tsq;

struct BenchmarkScenario
{
    const char* label = "";
    tests::performance::SyntheticProjectSpec spec;
};

struct MidiClipSelection
{
    std::string trackId;
    std::string clipId;
};

MidiClipSelection firstMidiClip (const core::sequencing::Project& project)
{
    for (const auto& track : project.tracks())
        if (! track.clips().empty())
            return MidiClipSelection { track.id(), track.clips().front().id() };

    return {};
}

void writeSummary (std::string label,
                   const tests::performance::SyntheticProjectSummary& summary,
                   std::size_t serializedBytes = 0)
{
    auto text = "SyntheticBenchmark::summary scenario=" + std::move (label) + " " + summary.label();
    if (serializedBytes > 0)
        text += " serializedBytes=" + std::to_string (serializedBytes);

    core::diagnostics::writePerformanceTrace (text, 0);
}

void benchmarkProjectScenario (const BenchmarkScenario& scenario)
{
    core::sequencing::Project project { "empty", "Empty" };
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "SyntheticBenchmark::build project scenario=" + std::string { scenario.label }
        };
        project = tests::performance::makeSyntheticProject (scenario.spec);
    }

    const auto summary = tests::performance::summarize (project);
    CHECK (summary.tracks == scenario.spec.trackCount);
    CHECK (summary.midiNotes == scenario.spec.midiNoteCount);
    CHECK (summary.automationPoints == scenario.spec.automationPointCount);

    std::string serialized;
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "SyntheticBenchmark::serialize project scenario=" + std::string { scenario.label }
        };
        serialized = core::serialization::ProjectSerializer::serialize (project);
    }

    writeSummary (scenario.label, summary, serialized.size());

    core::serialization::ProjectLoadResult loadResult {
        core::sequencing::Project { "empty-loaded", "Empty Loaded" },
        {}
    };
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "SyntheticBenchmark::deserialize project scenario=" + std::string { scenario.label }
        };
        loadResult = core::serialization::ProjectSerializer::deserializeWithWarnings (serialized);
    }

    CHECK (loadResult.warnings.empty());
    const auto loadedSummary = tests::performance::summarize (loadResult.project);
    CHECK (loadedSummary.tracks == summary.tracks);
    CHECK (loadedSummary.midiNotes == summary.midiNotes);
    CHECK (loadedSummary.automationPoints == summary.automationPoints);
    CHECK (loadedSummary.deviceSlots == summary.deviceSlots);

    const auto selection = firstMidiClip (loadResult.project);
    REQUIRE (! selection.trackId.empty());
    REQUIRE (! selection.clipId.empty());

    core::commands::ProjectCommandContext context { loadResult.project };
    core::commands::AddNoteCommand addNote {
        selection.trackId,
        selection.clipId,
        core::sequencing::MidiNote {
            "synthetic-benchmark-added-note",
            core::music_theory::MidiPitch::fromValue (72),
            tests::performance::detail::sixteenth (0),
            tests::performance::detail::sixteenthDuration(),
            96
        }
    };

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "SyntheticBenchmark::execute AddNote scenario=" + std::string { scenario.label }
        };
        REQUIRE (addNote.execute (context).succeeded());
    }
}

void benchmarkMidiRoundTrip (const BenchmarkScenario& scenario)
{
    auto clip = tests::performance::midiClip ("synthetic-midi-roundtrip-" + std::string { scenario.label },
                                              0,
                                              scenario.spec.midiNoteCount,
                                              0);

    std::vector<std::uint8_t> bytes;
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "SyntheticBenchmark::midi export notes=" + std::to_string (scenario.spec.midiNoteCount)
        };
        bytes = core::midi::MidiExporter::exportClipToBytes (clip);
    }

    core::midi::MidiImportResult importResult {
        core::sequencing::MidiClip {
            "empty-import",
            "Empty Import",
            {},
            tests::performance::detail::beats (1)
        },
        0,
        0,
        0
    };
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "SyntheticBenchmark::midi import notes=" + std::to_string (scenario.spec.midiNoteCount)
        };
        importResult = core::midi::MidiImporter::importClipFromBytes (
            bytes,
            core::midi::MidiImportOptions {
                "synthetic-midi-import-" + std::string { scenario.label },
                "Synthetic MIDI Import " + std::string { scenario.label },
                {}
            });
    }

    CHECK (importResult.importedNoteCount == static_cast<std::size_t> (scenario.spec.midiNoteCount));
    core::diagnostics::writePerformanceTrace (
        "SyntheticBenchmark::midi bytes notes=" + std::to_string (scenario.spec.midiNoteCount)
            + " bytes=" + std::to_string (bytes.size()),
        0);
}

void benchmarkTracktionSyncSmoke()
{
    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());

    auto project = tests::performance::makeSyntheticProject (tests::performance::SyntheticProjectSpec {
        "synthetic-sync",
        16,
        1000,
        0,
        0,
        0,
        0,
        false,
        false,
        false
    });

    const auto summary = tests::performance::summarize (project);
    writeSummary ("sync-smoke", summary);

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "SyntheticBenchmark::tracktion sync smoke " + summary.label()
        };
        REQUIRE (engine.syncProject (project));
    }

    CHECK (engine.getCurrentStatus().message.find ("Project synced") != std::string::npos);
}
}

TEST_CASE ("Synthetic project fixtures provide repeatable benchmark scenarios",
           "[integration][synthetic][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const auto scenarios = std::array<BenchmarkScenario, 3> {
        BenchmarkScenario {
            "small",
            tests::performance::SyntheticProjectSpec { "synthetic-small", 16, 100, 10 }
        },
        BenchmarkScenario {
            "medium",
            tests::performance::SyntheticProjectSpec { "synthetic-medium", 64, 1000, 100 }
        },
        BenchmarkScenario {
            "large",
            tests::performance::SyntheticProjectSpec { "synthetic-large", 128, 5000, 1000 }
        }
    };

    for (const auto& scenario : scenarios)
    {
        benchmarkProjectScenario (scenario);
        benchmarkMidiRoundTrip (scenario);
    }

    benchmarkTracktionSyncSmoke();
}
