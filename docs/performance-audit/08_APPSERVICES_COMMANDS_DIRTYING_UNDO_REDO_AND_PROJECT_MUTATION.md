# Segment 08 - AppServices Commands, Dirtying, Undo/Redo, And Project Mutation

Date: 2026-06-14

## Scope

Measured AppServices command execution, command-stack dirty notifications, undo/redo, validation, and common project mutation cost.

This segment focused on command execution paths for notes, clips, mixer edits, automation edits, routing edits, and device-chain model edits. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

Editing large MIDI clips can feel sluggish because note commands were doing broad vector work. In particular, moving or resizing a note inside a large clip used `MidiClip::replaceNote()`, which sorted the entire note vector after replacement. Resize did that even when the note's start position did not change.

The dirtying path was also inspected because every successful command, undo, redo, and rollback goes through the command-stack change callback. In AppServices that callback calls `markPlaybackProjectDirty()`, which marks playback dirty and schedules observed plugin-parameter state protection.

## Baseline Measurements

Added an integration performance probe with synthetic projects containing 32, 512, and 2048 notes in the active MIDI clip.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]" 2>&1 \
  | rg "AppCommandPerfProbe::|CommandDirtyProbe::dirty"
```

Filtered baseline before optimization:

| Probe | 32 Notes | 512 Notes | 2048 Notes |
|---|---:|---:|---:|
| `execute AddNote` | 0.520 ms | 0.996 ms | 3.681 ms |
| `execute MoveNote` | 0.168 ms | 0.563 ms | 2.556 ms |
| `execute ResizeNote` | 0.183 ms | 0.569 ms | 2.991 ms |
| `execute AddClip` | 0.153 ms | 0.078 ms | 0.136 ms |
| `execute MoveClip` | 0.108 ms | 0.069 ms | 0.069 ms |
| `execute ResizeClip` | 0.107 ms | 0.060 ms | 0.065 ms |
| `execute SetTrackMixerStrip` | 0.088 ms | 0.060 ms | 0.054 ms |
| `execute SetTrackRouting` | 0.193 ms | 0.088 ms | 0.104 ms |
| `execute SetTrackAutomationLane`, 128 points | 0.774 ms | 0.666 ms | 0.735 ms |
| `execute AddTrackDevice` | 0.238 ms | 0.081 ms | 0.080 ms |
| `execute SetTrackDeviceBypass` | 0.261 ms | 0.104 ms | 0.048 ms |
| `undo last command` | 0.099 ms | 0.062 ms | 0.118 ms |
| `redo last command` | 0.106 ms | 0.062 ms | 0.095 ms |

Dirty notification probe:

| Probe | Count |
|---|---:|
| `execute + undo + redo` | 3 dirty notifications |

Phase observations:

- `CommandStack::notifyChanged` calls AppServices dirtying once per successful execute, undo, redo, or rollback.
- `AppServices::markPlaybackProjectDirty` was usually tens of microseconds in the no-plugin synthetic probe.
- `MixerCommands::validateRouting` was microseconds for the two-track synthetic project.
- `MixerCommands::validateDeviceChain` was near-zero for the small synthetic chain.
- `ProjectMutationCommands::noteWithInferredHarmonicInterpretation` was roughly 0.15-0.33 ms when note harmonic metadata had to be inferred.
- The large scaling issue was note-vector mutation work in `MidiClip`, not playback sync. Non-device commands did not call `syncProject()` immediately.

## Changes Made

- Added opt-in phase timers for:
  - `CommandStack::execute <command name>`
  - `CommandStack::undo <command name>`
  - `CommandStack::redo <command name>`
  - `CommandStack::rollbackLastExecuted <command name>`
  - `CommandStack::notifyChanged`
  - `MixerCommands::validateRouting`
  - `MixerCommands::validateDeviceChain`
  - `ProjectMutationCommands::noteWithInferredHarmonicInterpretation`
  - `ProjectMutationCommands::scaleLibraryForProject`
- Added `tests/integration/AppServicesCommandPerformanceProbeTest.cpp` with the tag `[integration][commands][perf]`.
- `MidiClip::addNote()` now inserts the new note into the sorted note vector instead of appending and sorting the entire vector.
- `MidiClip::replaceNote()` now skips all sorting/repositioning when the replacement note keeps the same start position.
- `MidiClip::replaceNote()` now repositions only the changed note when its start changes, rather than sorting the entire vector.
- `MidiClip::moveNote()` now funnels through `replaceNote()`, so it uses the same single-note repositioning path.
- Dirtying behavior was intentionally left unchanged:
  - Every successful command/undo/redo still emits one command-stack change notification.
  - AppServices still marks playback dirty and schedules plugin-parameter protection for those notifications.
  - This avoids weakening the existing VST-state preservation safeguards.

Files changed:

- `src/core/commands/CommandStack.cpp`
- `src/core/commands/MixerCommands.cpp`
- `src/core/commands/ProjectMutationCommands.cpp`
- `src/core/sequencing/MidiClip.cpp`
- `tests/integration/AppServicesCommandPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/08_APPSERVICES_COMMANDS_DIRTYING_UNDO_REDO_AND_PROJECT_MUTATION.md`

## Before/After Result

Post-change filtered probe command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]" 2>&1 \
  | rg "AppCommandPerfProbe::|CommandDirtyProbe::dirty"
```

Post-change timings:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| `execute AddNote`, 2048 notes | 3.681 ms | 1.326 ms | about 64% faster |
| `execute MoveNote`, 2048 notes | 2.556 ms | 0.578 ms | about 77% faster |
| `execute ResizeNote`, 2048 notes | 2.991 ms | 0.110 ms | about 96% faster |
| `execute AddNote`, 512 notes | 0.996 ms | 0.858 ms | about 14% faster |
| `execute MoveNote`, 512 notes | 0.563 ms | 0.230 ms | about 59% faster |
| `execute ResizeNote`, 512 notes | 0.569 ms | 0.152 ms | about 73% faster |

Post-change 2048-note non-note command timings stayed low:

| Probe | After |
|---|---:|
| `execute AddClip` | 0.187 ms |
| `execute MoveClip` | 0.121 ms |
| `execute ResizeClip` | 0.107 ms |
| `execute SetTrackMixerStrip` | 0.081 ms |
| `execute SetTrackRouting` | 0.121 ms |
| `execute SetTrackAutomationLane`, 128 points | 0.770 ms |
| `execute AddTrackDevice` | 0.078 ms |
| `execute SetTrackDeviceBypass` | 0.079 ms |
| `undo last command` | 0.075 ms |
| `redo last command` | 0.084 ms |

The main win is for large-clip note mutation. Resizing a note no longer pays for a full note-vector sort when only the duration changed.

## Hot Paths Found

1. `MidiClip::replaceNote()` sorted every note after every replacement.
   - This made note move and resize scale poorly with large clips.
   - Resize was especially wasteful because start position usually stays unchanged.

2. `MidiClip::addNote()` appended and sorted every note.
   - Ordered insertion now preserves sort order without a full sort.

3. Dirty notifications are broad but predictable.
   - Execute, undo, and redo each emit one notification.
   - AppServices dirtying itself was not the primary bottleneck in the no-plugin probe.

4. Project-wide routing/device validation was not expensive in the small synthetic probe.
   - Larger routing topology work remains covered by Segment 06 and should be revisited with larger route graphs.

5. Device structural edits still sync playback immediately through AppServices.
   - That policy belongs with Segment 09's engine-sync work.

## Verification Run

Completed verification:

- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- Filtered `[integration][commands][perf]` probe with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][device-chain][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-list][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app`.
- Verified matching executable hash:
  - `e54c24f32cb5c320ff47264fba4aa9203a7600d8ea62b54288606df25e0a8448`

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- `shasum` emitted the known macOS locale warning before printing matching hashes.

## Remaining Risks

- Dirty categories were not introduced in this pass. AppServices still treats all command-stack mutations as playback-dirty.
- Plugin-heavy dirtying cost was not measured because the probe intentionally avoids real VST state testing.
- `noteWithInferredHarmonicInterpretation()` still rebuilds the scale library for commands that need inference. That is small in this probe but could be cached later if custom-scale projects make it significant.
- `MidiClip::findNoteById()` is still a linear search. The new ordered mutation path removes the full sort cost, but very large clips may eventually need an id-to-index cache or a different note storage model.
- Structural device edits still sync playback immediately; Segment 09 should decide which syncs can become incremental or deferred.

## Suggested Next Segment

Run Segment 09 - Tracktion Engine Sync And Playback Graph Materialization.

Reason:
- Command execution is now cheaper for large MIDI clips.
- The next large source of synchronous cost is engine sync: full rebuild vs in-place sync, plugin load/state restore, MIDI materialization, and whether device/clip/note edits can avoid full graph work.

## Performance Segment Handoff

- Segment: 08 - AppServices Commands, Dirtying, Undo/Redo, And Project Mutation
- User-visible symptom investigated: sluggish note edits in large MIDI clips and broad command dirtying.
- Baseline measurements: 2048-note AddNote was 3.681 ms, MoveNote was 2.556 ms, ResizeNote was 2.991 ms; execute+undo+redo emitted 3 dirty notifications.
- Hot paths found: full note-vector sorts in add/move/replace, resize sorting despite unchanged start, predictable broad dirty notifications, low synthetic validation cost.
- Changes made: command/validation/harmonic phase timers, command performance probe, sorted insertion for added notes, single-note reposition for moved notes, no-sort replace when start is unchanged.
- Files changed: `src/core/commands/CommandStack.cpp`, `src/core/commands/MixerCommands.cpp`, `src/core/commands/ProjectMutationCommands.cpp`, `src/core/sequencing/MidiClip.cpp`, `tests/integration/AppServicesCommandPerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, this report.
- Verification run: debug app build, core tests, full test preset, focused integration probes, debug-app sync, and matching executable hash completed.
- Before/after result: 2048-note AddNote dropped to 1.326 ms, MoveNote to 0.578 ms, ResizeNote to 0.110 ms.
- Remaining risks: dirty categories deferred, plugin-heavy dirtying not measured, linear note id lookup remains, structural device edits still sync playback.
- Suggested next segment: 09 - Tracktion Engine Sync And Playback Graph Materialization.
