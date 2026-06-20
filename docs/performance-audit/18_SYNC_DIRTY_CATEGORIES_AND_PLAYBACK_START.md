# Segment 18 - Sync Dirty Categories And Playback Start

Date: 2026-06-14

## Scope

Measured Tracktion sync paths, playback-start allocation behavior, and AppServices command dirtying.

This segment did not run the expensive Synthesizer/VST parameter-wipe stress harness. The work focused on making command dirtying more precise so editor-only harmonic changes do not force playback graph work.

## User-Visible Symptom Investigated

Edits that only change score/theory metadata, such as key centers, scale/mode regions, chord regions, or rhythm-grid settings, were treated the same as note, clip, mixer, and device edits. That meant the next playback sync could rebuild or touch the playback graph even when Tracktion did not need the changed data.

This matters for the piano roll because harmonic-region editing can be frequent, and it also intersects with plugin-state safety: unnecessary playback syncs increase the number of times live plugin state has to be preserved around Tracktion graph work.

## Baseline Measurements

Fresh pre-change sync probe:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][sync][perf]"
```

Relevant pre-change results from the start of this segment:

| Probe | Before |
|---|---:|
| Full sync, 4 tracks / 4 clips / 64 notes per clip | 130.024 ms |
| Mixer-only in-place sync, 4 tracks / 4 clips / 64 notes per clip | 1.973 ms |
| One-note-change in-place sync, 4 tracks / 4 clips / 64 notes per clip | 29.084 ms |
| Unchanged in-place sync, 4 tracks / 4 clips / 64 notes per clip | 1.572 ms |
| Full sync, 8 tracks / 8 clips / 128 notes per clip | 805.968 ms |
| Mixer-only in-place sync, 8 tracks / 8 clips / 128 notes per clip | 3.754 ms |
| One-note-change in-place sync, 8 tracks / 8 clips / 128 notes per clip | 92.632 ms |
| Unchanged in-place sync, 8 tracks / 8 clips / 128 notes per clip | 10.789 ms |

Playback-start allocation baseline:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][memory][perf]"
```

| Probe | Before |
|---|---:|
| Playback start, synced project | 29,100 allocations / 1,505,144 bytes |

## Hot Paths Found

1. Command dirtying was binary.
   - Every command callback called `AppServices::markPlaybackProjectDirty()`.
   - The app could not distinguish playback-relevant edits from editor-only theory metadata.

2. Tracktion sync reuse is effective for unchanged and mixer-only cases.
   - Current in-place sync can reuse all track clip materialization when clip/note/audio data has not changed.
   - Mixer-only in-place sync is already in the low single-digit millisecond range in debug probes.

3. One-note-change sync still rebuilds the changed track's Tracktion clip materialization.
   - The in-place path reuses unchanged tracks, but a changed note causes the whole owning track's clips to be cleared and recreated.
   - This is the remaining engine-sync target after category dirtying.

4. Playback start allocations remain Tracktion/engine-side work.
   - This segment did not reduce synced-project playback-start allocations.
   - The probe still attributes the synced playback start to about 29,100 allocations / 1.5 MB.

## Changes Made

- Added `core::commands::PlaybackSyncCategory`.
  - Playback-relevant categories: unknown, track structure, clip data, note data, automation, mixer, routing, device chain, tempo map, and time-signature map.
  - Editor/theory-only categories: harmonic structure, editor rhythm, and custom scales.
- Added category-aware command callbacks in `CommandStack`.
  - Existing no-argument callbacks still work.
  - Category callbacks are traced as `CommandStack::change category=...` when performance tracing is enabled.
- Removed scoped performance timers from app boot and JUCE timer-callback paths after the debug app exposed a startup crash in `ScopedPerformanceTimer` destruction on the message thread.
  - The category traces and focused command probes remain available.
  - Startup/timer callback timing should be reintroduced with safer instrumentation rather than stack-local scoped timers in those paths.
- Classified command headers by playback category.
  - Notes/chords/arpeggiation/transposition: `noteData`.
  - Clips: `clipData`.
  - Mixer/routing/device/automation commands: matching playback categories.
  - Tempo/time signature commands: matching map categories.
  - Key center, scale/mode, chord-region, globalized chord progression, custom scale, and rhythm-grid commands: editor/theory categories.
- Changed `AppServices` dirty tracking from a bare boolean to boolean plus category mask.
  - Playback-relevant commands still dirty the playback project and schedule plugin-state protection.
  - Harmonic/editor-only commands now skip playback dirtying when no playback sync is needed.
  - `syncPlaybackProjectIfNeeded()` clears the category mask after successful sync.
  - Added test-facing dirty accessors for regression probes.
- Extended the command performance probe.
  - Verifies key center, scale/mode, chord region, and rhythm-grid commands do not dirty playback.
  - Verifies a following note command still dirties playback.
  - Verifies command stack undo/redo notifications preserve the command category.

Files changed:

- `src/core/commands/Command.h`
- `src/core/commands/CommandStack.h`
- `src/core/commands/CommandStack.cpp`
- Command category overrides in `src/core/commands/*.h`
- `src/app/AppServices.h`
- `src/app/AppServices.cpp`
- `tests/integration/AppServicesCommandPerformanceProbeTest.cpp`
- `src/ui/MainComponent.cpp`
- `src/ui/TransportComponent.cpp`
- `src/ui/BrowserPanelComponent.cpp`
- `src/ui/PluginBrowserComponent.cpp`
- This report.

## Before/After Result

Focused command probe:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]"
```

Key after-change traces:

| Probe | After |
|---|---:|
| Add key center region dirty handling | skipped playback dirty; no-op sync |
| Add scale/mode region dirty handling | skipped playback dirty; no-op sync |
| Add chord region dirty handling | skipped playback dirty; no-op sync |
| Set project rhythm settings dirty handling | skipped playback dirty; no-op sync |
| Add note after harmonic edits | playback dirty set as `note-data` |

Representative focused timings:

| Probe | After |
|---|---:|
| Add key center region command | 0.182 ms |
| Add scale/mode region command | 0.302 ms |
| Add chord region command | 0.339 ms |
| Set project rhythm settings command | 0.216 ms |
| `syncPlaybackProjectIfNeeded()` after each skipped command | 0.000 ms |

Post-change sync probe:

| Probe | After |
|---|---:|
| Full sync, 4 tracks / 4 clips / 64 notes per clip | 80.083 ms |
| Mixer-only in-place sync, 4 tracks / 4 clips / 64 notes per clip | 3.400 ms |
| One-note-change in-place sync, 4 tracks / 4 clips / 64 notes per clip | 19.848 ms |
| Unchanged in-place sync, 4 tracks / 4 clips / 64 notes per clip | 3.626 ms |
| Full sync, 8 tracks / 8 clips / 128 notes per clip | 528.809 ms |
| Mixer-only in-place sync, 8 tracks / 8 clips / 128 notes per clip | 6.266 ms |
| One-note-change in-place sync, 8 tracks / 8 clips / 128 notes per clip | 67.196 ms |
| Unchanged in-place sync, 8 tracks / 8 clips / 128 notes per clip | 4.164 ms |

The category work does not directly optimize Tracktion materialization. The sync numbers are still useful because they confirm the existing in-place behavior remains intact, and they show the remaining note-change cost is still track-level clip recreation.

Playback-start allocation after-change:

| Probe | After |
|---|---:|
| Playback start, synced project | 29,100 allocations / 1,505,144 bytes |

Playback-start allocation is unchanged in this segment.

## Verification Run

Completed verification:

- `cmake --build --preset tests --target tsq_core_tests`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 ./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]"`
- `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 ./out/build/debug/tests/tsq_engine_integration_tests "[integration][sync][perf]"`
- `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 ./out/build/debug/tests/tsq_engine_integration_tests "[integration][memory][perf]"`

Results:

- Core test target built.
- Integration test target built.
- Focused command category probe passed.
- Sync performance probe passed.
- Memory allocation probe passed.
- Debug app launch was reproduced locally after the follow-up crash fix and stayed up past the previous crash point, including MIDI input refresh.

Known existing warnings/notes:

- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- JUCE assertion in `juce_Component.cpp:3022` during the memory allocation probe.
- Existing unused-variable warning in `VstStateRegressionTest.cpp`.

## Remaining Risks

- One-note-change sync still rebuilds all Tracktion clips on the changed track. Clip-level or note-level materialization reuse remains the most direct sync optimization.
- Playback-start allocation cost remains unchanged and should be separated into Tracktion transport start, automation snapshot, meter client refresh, and plugin observer phases if it becomes user-visible.
- The category list is intentionally conservative. Unknown commands still require playback sync. Future commands should declare categories as they are added.
- Custom scales are currently treated as editor/theory-only. If future playback features use scale data directly, that category will need to become playback-relevant or split into a narrower category.
- Startup and JUCE timer-callback timing is temporarily less granular because the stack-local scoped timer pattern caused a boot crash in the debug app.

## Suggested Next Segment

Run Segment 19 - Dense Paint, Dirty Regions, And UI Allocation Pass.

Reason:

- Segment 18 removed unnecessary playback dirtying for harmonic/editor-only edits.
- The remaining sync work is known but requires a deeper Tracktion clip-level materialization design.
- Dense UI paints and paint-time allocations remain visibly large in the memory probe: piano-roll warm paint still shows 10,384 allocations / 1,324,637 bytes, and timeline warm paint still shows 25,892 allocations / 1,168,735 bytes.

## Performance Segment Handoff

- Segment: 18 - Sync Dirty Categories And Playback Start
- User-visible symptom investigated: playback sync work after editor-only harmonic/theory edits.
- Baseline measurements: harmonic commands previously used the same binary dirty path as playback edits; synced playback start measured 29,100 allocations / 1,505,144 bytes.
- Hot paths found: binary dirtying, changed-track Tracktion clip materialization, playback-start allocations.
- Changes made: command playback sync categories, category-aware command callbacks, AppServices dirty category mask, harmonic/editor skip path, focused regression probe.
- Files changed: `src/core/commands/Command.*`, command headers, `src/app/AppServices.*`, `tests/integration/AppServicesCommandPerformanceProbeTest.cpp`, this report.
- Verification run: core build, integration build, command category probe, sync probe, memory allocation probe.
- Before/after result: key/scale/chord/rhythm commands now skip playback dirtying and produce 0.000 ms no-op syncs; note commands still dirty playback.
- Remaining risks: note-change sync is still track-level, playback-start allocations unchanged.
- Suggested next segment: 19 - Dense Paint, Dirty Regions, And UI Allocation Pass.
