# Segment 14 - Memory, Allocation, And Data Structure Scalability

Date: 2026-06-14

## Scope

Measured broad allocation pressure across dense piano-roll paint, timeline paint, note-edit commands, browser refresh, and playback start.

This segment did not run the expensive Synthesizer/VST parameter-wipe stress harness.

## User-Visible Symptom Investigated

The app can feel laggy during ordinary editing because small actions may trigger repeated allocations, vector growth, model copies, label formatting, or broad engine/UI work. This segment focused on finding app-owned allocation spikes that can be reduced without changing DAW behavior.

## Baseline Measurements

Added `tests/integration/MemoryAllocationPerformanceProbeTest.cpp` with tag `[integration][memory][perf]`. The probe temporarily counts allocations by wrapping global `operator new` while scoped probes are active.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][memory][perf]"
```

Filtered baseline before optimization:

| Probe | Baseline allocations | Baseline bytes |
|---|---:|---:|
| Piano-roll warm paint, 2,000 notes | 12,339 | 1,371,725 |
| Timeline warm paint, 16 tracks / 128 clips | 25,914 | 1,169,487 |
| Command execute, ten note edits | 1,495 | 590,160 |
| Command move, ten notes | 51 | 4,096 |
| Browser warm refresh, 2,000 plugins | 406 | 38,934 |
| Playback start, synced project | 29,102 | 1,505,256 |

## Changes Made

- Added `[integration][memory][perf]` allocation probe covering UI paint, command mutation, browser refresh, and playback start.
- Changed piano-roll note painting to iterate note-render bounds through a callback in the non-drag paint path instead of allocating a temporary bounds vector per note.
- Kept MIDI clip note storage sorted with insertion-point replacement rather than sorting the entire vector after every add/move.
- Added `MidiClip::reserveNotes()` and used it for MIDI import, project load, and synthetic dense test clips so loaded/imported clips keep edit headroom.
- Changed note/clip/track/custom-scale removal paths to move removed values out of containers instead of copying them before erase.
- Cached the built-in scale library and avoided rebuilding it for every note harmonic inference.
- Changed note harmonic interpretation hot paths to read scale definitions directly instead of constructing temporary scale instances and pitch-class vectors.
- Changed `ScopedPerformanceTimer` to avoid owning allocations for static labels and to only compose dynamic labels when tracing is enabled.
- Changed dynamic `CommandStack` timer labels to use prefix/suffix timer construction.
- Added a small timeline automation target label cache.

Files changed:

- `src/core/commands/CommandStack.cpp`
- `src/core/commands/ProjectMutationCommands.cpp`
- `src/core/diagnostics/PerformanceTrace.cpp`
- `src/core/diagnostics/PerformanceTrace.h`
- `src/core/midi/MidiImporter.cpp`
- `src/core/sequencing/MidiClip.cpp`
- `src/core/sequencing/MidiClip.h`
- `src/core/sequencing/NoteHarmonicInterpretation.cpp`
- `src/core/sequencing/Project.cpp`
- `src/core/sequencing/Track.cpp`
- `src/core/serialization/ProjectSerializer.cpp`
- `src/ui/PianoRollComponent.cpp`
- `src/ui/TimelineComponent.cpp`
- `src/ui/TimelineComponent.h`
- `tests/integration/MemoryAllocationPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/14_MEMORY_ALLOCATION_AND_DATA_STRUCTURE_SCALABILITY.md`

## Before/After Result

Post-change focused probe:

| Probe | Before allocations / bytes | After allocations / bytes | Result |
|---|---:|---:|---|
| Piano-roll warm paint, 2,000 notes | 12,339 / 1,371,725 | 10,384 / 1,324,637 | about 16% fewer allocations |
| Timeline warm paint, 16 tracks / 128 clips | 25,914 / 1,169,487 | 25,892 / 1,168,735 | about the same |
| MIDI clip direct ten note inserts | not measured | 0 / 0 | direct insert is allocation-free with headroom |
| Direct AddNote command, ten note edits | not measured | 3 / 112 | command logic is now very light after warm-up |
| AppServices command execute, ten note edits | 1,495 / 590,160 | 24 / 2,320 | allocation spike removed |
| Command move, ten notes | 51 / 4,096 | 21 / 2,656 | fewer timer/command allocations |
| Browser warm refresh, 2,000 plugins | 406 / 38,934 | 401 / 38,710 | about the same |
| Playback start, synced project | 29,102 / 1,505,256 | 29,100 / 1,505,144 | about the same |

The largest app-owned fix was the note-edit spike. The probe showed raw dense MIDI insertion is allocation-free when the clip has edit headroom, and direct warmed AddNote execution is only 3 allocations / 112 bytes. The previous AppServices spike was largely dense vector growth after exact-fit clip copies; reserving edit headroom for loaded/imported/synthetic clips removes that pattern.

## Hot Paths Found

1. AppServices note editing was paying for dense note-vector growth after exact-fit copies.
   - Fixed by preserving edit headroom through `MidiClip::reserveNotes()` in load/import/synthetic fixture paths.

2. AddNote harmonic inference rebuilt the built-in scale library and temporary scale data.
   - Fixed with a cached built-in scale library and direct scale-definition reads in the hottest interpretation paths.

3. Piano-roll warm paint allocated a temporary note-bounds vector per rendered note.
   - Fixed with callback iteration for the normal paint path.

4. Static performance timers still owned strings.
   - Fixed timer constructors so static labels are string views and dynamic labels compose only when tracing is enabled.

5. Playback start allocations are dominated by engine/Tracktion graph work.
   - Measured, but not changed in this segment.

6. Timeline paint allocation count remains high.
   - A small automation-label cache helped only marginally; most remaining allocation appears tied to drawing/text and broader paint structure.

## Verification Run

Completed verification:

- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 ./out/build/debug/tests/tsq_engine_integration_tests "[integration][memory][perf]"`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- Source-only whitespace check: `git diff --check -- src tests docs`
- Focused integration bundle with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`:
  - `[integration][memory][perf]`
  - `[integration][piano-roll][perf]`
  - `[integration][commands][perf]`
  - `[integration][automation][perf]`
  - `[integration][midi-transport][perf]`
  - `[integration][sync][perf]`
- `cmake --build --preset debug --target tsq_app`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app`.
- Verified matching executable hash:
  - `c8d99f2e4aa5304be6ab566f81a1eb167a26bcaa6ea33301654e3666d2ad39a2`

Focused integration result:

- 6 test cases passed.
- 125 assertions passed.

Known existing warnings/notes:

- Existing warning in `VstStateRegressionTest.cpp` about an unused variable when that file is compiled.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.

## Remaining Risks

- Piano-roll and timeline warm paints still allocate heavily; future work should use Instruments Allocations to separate JUCE text/path work from app-created temporary containers.
- Playback start still allocates about 1.5 MB in this synthetic scenario and likely belongs to the Tracktion sync/playback graph boundary.
- `ScaleInstance` still owns scale definitions for general callers. Only hot note-interpretation paths avoid the copy.
- Dense clips loaded before this change may still have exact-fit note vectors until they are saved/reloaded or otherwise reconstructed.
- The allocation probe counts process-wide allocations during scoped regions, so it is excellent for regression detection but not a substitute for stack-attributed Instruments sessions.

## Suggested Next Segment

Run Segment 15 - Synthetic Project Fixtures And Regression Benchmarks.

Reason:

- The audit now has many focused probes. The next useful step is consolidating large-project fixtures and thresholds so future optimizations can be compared consistently across contexts.

## Performance Segment Handoff

- Segment: 14 - Memory, Allocation, And Data Structure Scalability
- User-visible symptom investigated: lag from allocation churn, dense note-vector growth, temporary UI paint structures, and command-side model copies.
- Baseline measurements: AddNote ten-edit command path allocated 1,495 times / 590,160 bytes; piano-roll warm paint allocated 12,339 times / 1,371,725 bytes; playback start allocated 29,102 times / 1,505,256 bytes.
- Hot paths found: exact-fit dense clip vectors after load/copy, repeated built-in scale-library construction, temporary note-render-bounds vectors, static timer label ownership, engine-side playback start allocations.
- Changes made: memory allocation probe, note edit headroom, move-on-remove, built-in scale-library cache, direct scale-definition note interpretation, piano-roll bounds callback iteration, timer label allocation reduction, small timeline automation label cache.
- Files changed: core command/diagnostics/midi/sequencing/serialization files, `PianoRollComponent.cpp`, `TimelineComponent.*`, memory allocation probe, `tests/CMakeLists.txt`, this report.
- Verification run: debug integration build, memory probe, core tests preset, source-only diff check, focused memory/piano-roll/commands/automation/transport/sync integration bundle, debug app build/sync, matching executable hash.
- Before/after result: AppServices ten-note AddNote path dropped from 1,495 allocations / 590,160 bytes to 24 allocations / 2,320 bytes; direct warmed AddNote execution is 3 allocations / 112 bytes; raw dense note insertion is 0 allocations.
- Remaining risks: UI paint and playback start allocations remain high; stack-attributed Instruments sessions are still needed for the remaining paint/engine costs.
- Suggested next segment: 15 - Synthetic Project Fixtures And Regression Benchmarks.
