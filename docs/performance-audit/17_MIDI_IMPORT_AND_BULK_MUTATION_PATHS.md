# Segment 17 - MIDI Import And Bulk Mutation Paths

Date: 2026-06-14

## Scope

Measured and optimized dense MIDI import and related bulk note construction paths.

This segment did not run the expensive Synthesizer/VST parameter-wipe stress harness. The work focused on the large synthetic MIDI roundtrip from Segment 15, where importing 5,000 notes was the largest measured remaining hot path.

## User-Visible Symptom Investigated

Importing a dense MIDI file can freeze the app. Segment 15 measured a 45 KB / 5,000-note MIDI byte stream taking about 1.5-1.7 seconds to import in a debug build.

## Baseline Measurements

Fresh pre-change synthetic benchmark command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][synthetic][perf]"
```

Relevant pre-change results:

| Probe | Before |
|---|---:|
| MIDI import, 100 notes | 1.279 ms |
| MIDI import, 1,000 notes | 64.395 ms |
| MIDI import, 5,000 notes | 1,484.435 ms |
| Build large synthetic project | 31.453 ms |
| Deserialize large synthetic project | 325.924 ms |

The 5,000-note result confirmed the Segment 15 finding: import was scaling much worse than linearly.

## Hot Paths Found

1. MIDI import used one-by-one `MidiClip::addNote()` calls.
   - Each call scanned existing notes for duplicate IDs.
   - Each call inserted into the sorted note vector.
   - For thousands of imported notes, this became quadratic model work after parsing had already succeeded.

2. Project deserialization used the same one-by-one note add pattern.
   - Segment 17 targeted the importer first, but the same bulk clip API can keep project load from regressing with denser clips.

3. The synthetic fixture generator also used one-by-one note add.
   - This made benchmark project construction include test-helper note insertion cost.

## Changes Made

- Added `MidiClip::addNotes(std::vector<MidiNote>)`.
  - Validates note bounds before mutation.
  - Validates duplicate note IDs across existing and incoming notes before mutation.
  - Stable-sorts incoming notes by start tick once.
  - Merges incoming notes with existing sorted notes in one pass.
- Changed `MidiImporter::buildClip()` to materialize imported notes into a vector, then call `addNotes()` once.
- Added import phase timers:
  - `MidiImporter::readHeader`
  - `MidiImporter::readNotes`
  - `MidiImporter::buildClip`
  - `MidiImporter::buildClip sort imported notes`
  - `MidiImporter::buildClip materialize notes`
  - `MidiImporter::buildClip bulk add notes`
  - `MidiImporter::importClipFromBytes`
- Changed project deserialization to bulk-add notes after reading all note JSON.
- Changed synthetic MIDI clip fixture construction to bulk-add generated notes.
- Added focused integration probe `tests/integration/MidiImportPerformanceProbeTest.cpp` with tag `[integration][midi-import][perf]`.
- Added unit coverage for `MidiClip::addNotes()` sorted order, duplicate rejection, and no partial mutation on failure.

Files changed:

- `src/core/sequencing/MidiClip.h`
- `src/core/sequencing/MidiClip.cpp`
- `src/core/midi/MidiImporter.cpp`
- `src/core/serialization/ProjectSerializer.cpp`
- `tests/core/ProjectModelTest.cpp`
- `tests/integration/MidiImportPerformanceProbeTest.cpp`
- `tests/performance/SyntheticProjectFixtures.h`
- `tests/CMakeLists.txt`
- This report.

## Before/After Result

Focused MIDI import probe:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][midi-import][perf]"
```

| Probe | After |
|---|---:|
| Focused MIDI import, 5,000 notes | 44.986 ms |

Focused phase breakdown:

| Phase | After |
|---|---:|
| `MidiImporter::readHeader` | 0.007 ms |
| `MidiImporter::readNotes` | 20.412 ms |
| `MidiImporter::buildClip sort imported notes` | 0.097 ms |
| `MidiImporter::buildClip materialize notes` | 4.631 ms |
| `MidiImporter::buildClip bulk add notes` | 17.772 ms |
| `MidiImporter::buildClip` | 23.077 ms |
| `MidiImporter::importClipFromBytes` | 44.966 ms |

Full synthetic benchmark after the change:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| MIDI import, 100 notes | 1.279 ms | 1.652 ms | noisy/small |
| MIDI import, 1,000 notes | 64.395 ms | 7.490 ms | about 8.6x faster |
| MIDI import, 5,000 notes | 1,484.435 ms | 34.060 ms | about 43.6x faster |
| Build large synthetic project | 31.453 ms | 16.828 ms | about 46% faster |
| Deserialize large synthetic project | 325.924 ms | 308.478 ms | about 5% faster |

The 5,000-note import path moved from seconds-scale UI-freeze territory to tens of milliseconds in the debug benchmark.

## Verification Run

Completed verification:

- `cmake --build --preset tests --target tsq_core_tests`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `ctest --preset tests --output-on-failure`
- `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 ./out/build/debug/tests/tsq_engine_integration_tests "[integration][midi-import][perf]"`
- `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 ./out/build/debug/tests/tsq_engine_integration_tests "[integration][synthetic][perf]"`

Results:

- Core tests passed.
- Focused MIDI import performance probe passed.
- Full synthetic performance benchmark passed.

Known existing warnings/notes:

- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- JUCE no-symbol archive warnings for headless ARA/LV2 objects.
- Existing unused-variable warning in `VstStateRegressionTest.cpp`.

## Remaining Risks

- `MidiImporter::readNotes` is now one of the largest import phases. It still uses a `std::map<int, std::deque<ActiveNote>>` active-note structure that could be replaced with fixed channel/pitch storage later.
- `MidiClip::addNotes()` still performs duplicate-ID validation by sorting IDs. That is safe and much faster than the previous per-note scan, but it remains visible in debug builds for very dense imports.
- Project deserialization improved slightly for the large synthetic case, but JSON parsing/serialization remains a separate IO roadmap item.
- Import still happens synchronously from the caller's perspective. Very large external MIDI files may still need async/progressive UI treatment after the model path is optimized.

## Suggested Next Segment

Run Segment 18 - Sync Dirty Categories And Playback Start.

Reason:

- MIDI import's largest measured model-side issue is now fixed.
- The next roadmap item is playback graph materialization and dirty-category staging, where Segment 14 still measured playback start at 29,102 allocations / 1,505,256 bytes and Segment 15 measured Tracktion sync smoke at 115.697 ms.

## Performance Segment Handoff

- Segment: 17 - MIDI Import And Bulk Mutation Paths
- User-visible symptom investigated: dense MIDI import freezing the app.
- Baseline measurements: 5,000-note MIDI import was 1,484.435 ms in the fresh pre-change synthetic benchmark.
- Hot paths found: one-by-one note insertion caused duplicate-ID scans and sorted vector insertion per imported note.
- Changes made: bulk `MidiClip::addNotes()`, importer phase timers, importer/deserializer/fixture bulk note construction, focused MIDI import perf probe, core bulk-add tests.
- Files changed: `src/core/sequencing/MidiClip.*`, `src/core/midi/MidiImporter.cpp`, `src/core/serialization/ProjectSerializer.cpp`, `tests/core/ProjectModelTest.cpp`, `tests/integration/MidiImportPerformanceProbeTest.cpp`, `tests/performance/SyntheticProjectFixtures.h`, `tests/CMakeLists.txt`, this report.
- Verification run: core build/tests, debug integration build, focused MIDI import perf probe, synthetic benchmark.
- Before/after result: 5,000-note synthetic MIDI import dropped from 1,484.435 ms to 34.060 ms; focused 5,000-note import probe measured 44.986 ms.
- Remaining risks: import parsing and duplicate-ID validation are now the main import phases; import remains synchronous.
- Suggested next segment: 18 - Sync Dirty Categories And Playback Start.
