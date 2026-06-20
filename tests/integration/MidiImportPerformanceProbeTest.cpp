#include "../performance/SyntheticProjectFixtures.h"

#include "core/diagnostics/PerformanceTrace.h"
#include "core/midi/MidiExporter.h"
#include "core/midi/MidiImporter.h"
#include "core/time/Tick.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
using namespace tsq;

bool notesAreSortedByStart (const core::sequencing::MidiClip& clip)
{
    return std::is_sorted (clip.notes().begin(), clip.notes().end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInClip() < rhs.startInClip();
    });
}
}

TEST_CASE ("MIDI import performance probe covers dense note roundtrip",
           "[integration][midi-import][perf]")
{
    constexpr auto noteCount = 5000;

    const auto sourceClip = tests::performance::midiClip ("midi-import-perf-source",
                                                          0,
                                                          noteCount,
                                                          0);

    std::vector<std::uint8_t> bytes;
    {
        core::diagnostics::ScopedPerformanceTimer timer { "MidiImportPerfProbe::export fixture notes=5000" };
        bytes = core::midi::MidiExporter::exportClipToBytes (sourceClip);
    }

    auto importResult = [&] {
        core::diagnostics::ScopedPerformanceTimer timer { "MidiImportPerfProbe::import notes=5000" };
        return core::midi::MidiImporter::importClipFromBytes (
            bytes,
            core::midi::MidiImportOptions {
                "midi-import-perf-result",
                "MIDI Import Perf Result",
                {}
            });
    }();

    REQUIRE (importResult.importedNoteCount == static_cast<std::size_t> (noteCount));
    REQUIRE (importResult.clip.notes().size() == static_cast<std::size_t> (noteCount));
    CHECK (notesAreSortedByStart (importResult.clip));
    CHECK (importResult.sourceTrackCount == 1);
    CHECK (importResult.sourcePulsesPerQuarterNote == core::time::ticksPerQuarterNote);

    core::diagnostics::writePerformanceTrace (
        "MidiImportPerfProbe::bytes notes=5000 bytes=" + std::to_string (bytes.size()),
        0);
}
