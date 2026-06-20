# Segment 09 - Tracktion Engine Sync And Playback Graph Materialization

Date: 2026-06-14

## Scope

Measured Tracktion playback sync, full edit rebuilds, compatible in-place syncs, and MIDI clip materialization.

This segment focused on `TracktionPlaybackEngine::syncProject()`, especially the difference between a full edit rebuild and the existing in-place path. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

Pressing Play after edits can feel delayed when the playback project is dirty. The previous in-place path preserved loaded plugins when possible, but it still cleared and recreated every Tracktion MIDI/audio clip on every compatible sync.

That meant a mixer-only edit, automation-only edit, or unchanged dirty sync could still pay almost the same MIDI materialization cost as a note edit.

## Baseline Measurements

Added an integration performance probe with synthetic MIDI projects:

- Small: 4 tracks, 4 clips per track, 64 notes per clip, 1,024 notes total.
- Large: 8 tracks, 8 clips per track, 128 notes per clip, 8,192 notes total.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][sync][perf]" 2>&1 \
  | rg "TracktionSyncPerfProbe::|TracktionPlaybackEngine::syncProject|TracktionPlaybackEngine::createProjectTrackClips"
```

Filtered baseline before optimization:

| Probe | 1,024 Notes | 8,192 Notes |
|---|---:|---:|
| Full sync | 59.286 ms | 521.873 ms |
| In-place mixer-only sync | 53.750 ms | 459.872 ms |
| In-place one-note-change sync | 51.396 ms | 448.567 ms |
| In-place unchanged sync | 54.096 ms | 416.948 ms |

Phase observations:

- Full sync was dominated by `createProjectTrackClips()`.
- In-place sync still called `clearTrackClips()` and `createProjectTrackClips()` for every track.
- In the 8,192-note project, each track's MIDI materialization was roughly 50-65 ms.
- `canSyncProjectInPlace()`, tempo setup, time signature setup, track lookup, meter client rebuild, and plugin-state capture/restore were tiny in the no-plugin probe.

## Changes Made

- Added opt-in phase timers for:
  - `TracktionPlaybackEngine::syncProject initialize`
  - `TracktionPlaybackEngine::canSyncProjectInPlace`
  - `TracktionPlaybackEngine::syncProject reset edit graph`
  - tempo and time-signature configuration
  - audio-track ensure and track lookup
  - in-place capture/stop/tempo/time-signature/track-lookup/dispatch/restore phases
  - `TracktionPlaybackEngine::finishProjectSync` phases
  - per-track `configureProjectTrack()`
  - per-track `createProjectTrackClips()`
- Added `tests/integration/TracktionSyncPerformanceProbeTest.cpp` with the tag `[integration][sync][perf]`.
- Added in-place clip materialization reuse:
  - The engine compares the current project track to the previous synced project snapshot.
  - Reuse only happens when the same non-master track ID is at the same Tracktion audio-track index.
  - Reordered, added, removed, or replaced tracks still rebuild their clips.
  - Changed clip playback data still rebuilds that track's clips.
- Added a trace summary for in-place clip reuse:
  - `TracktionPlaybackEngine::syncProjectInPlace clip materialization summary reusedTracks=N rebuiltTracks=M`

Playback materialization equality checks include:

- MIDI clip ID, name, start, length, loop enabled/duration.
- MIDI note ID, pitch, start, duration, velocity.
- Audio clip ID, name, source, start, length, offset, loop, stretch, gain.

Note spelling and harmonic metadata are intentionally not part of the playback equality check because they do not affect Tracktion MIDI event materialization.

Files changed:

- `src/engine/TracktionPlaybackEngine.cpp`
- `tests/integration/TracktionSyncPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/09_TRACKTION_ENGINE_SYNC_AND_PLAYBACK_GRAPH_MATERIALIZATION.md`

## Before/After Result

Post-change filtered probe command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][sync][perf]" 2>&1 \
  | rg "TracktionSyncPerfProbe::|TracktionPlaybackEngine::syncProjectInPlace clip materialization summary|TracktionPlaybackEngine::createProjectTrackClips"
```

Post-change timings:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| In-place mixer-only sync, 1,024 notes | 53.750 ms | 1.008 ms | about 98% faster |
| In-place one-note-change sync, 1,024 notes | 51.396 ms | 15.627 ms | about 70% faster |
| In-place unchanged sync, 1,024 notes | 54.096 ms | 0.728 ms | about 99% faster |
| In-place mixer-only sync, 8,192 notes | 459.872 ms | 2.302 ms | about 99% faster |
| In-place one-note-change sync, 8,192 notes | 448.567 ms | 59.565 ms | about 87% faster |
| In-place unchanged sync, 8,192 notes | 416.948 ms | 2.642 ms | about 99% faster |

Materialization summaries after optimization:

| Probe | Reused Tracks | Rebuilt Tracks |
|---|---:|---:|
| Mixer-only sync, 1,024 notes | 4 | 0 |
| One-note-change sync, 1,024 notes | 3 | 1 |
| Unchanged sync, 1,024 notes | 4 | 0 |
| Mixer-only sync, 8,192 notes | 8 | 0 |
| One-note-change sync, 8,192 notes | 7 | 1 |
| Unchanged sync, 8,192 notes | 8 | 0 |

Full sync remains intentionally full:

| Probe | Before | After |
|---|---:|---:|
| Full sync, 1,024 notes | 59.286 ms | 72.463 ms |
| Full sync, 8,192 notes | 521.873 ms | 481.152 ms |

The full-sync variance is expected debug-build timing noise. The optimization targets compatible in-place syncs only.

## Hot Paths Found

1. `createProjectTrackClips()` dominates MIDI-heavy sync.
   - Tracktion MIDI clip insertion and sequence note insertion scale directly with materialized notes.

2. In-place sync was not clip-incremental.
   - It avoided plugin rebuilds but still rebuilt every clip on every track.

3. Full sync is still expensive by design.
   - Full graph rebuild recreates the edit, audio tracks, clips, meters, and any devices.

4. Meter and automation setup were not expensive in this synthetic no-plugin probe.
   - `finishProjectSync` phases were usually microseconds to low hundreds of microseconds.

5. Real VST plugin costs were not measured in this segment.
   - Plugin load/state restore remains a separate risk for plugin-heavy projects.

## Verification Run

Completed verification:

- `cmake --build --preset debug --target tsq_engine_integration_tests`
- Filtered `[integration][sync][perf]` probe with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][sync][perf]"`
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][device-chain][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-list][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app`.
- Verified matching executable hash:
  - `9f0b9715bd90e57b8983ba4213100c79ea0be76c3462158f4cd8edd9b289f280`

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- `shasum` emitted the known macOS locale warning before printing matching hashes.

## Remaining Risks

- Full sync is still synchronous and still materializes every clip.
- In-place sync still rebuilds an entire track's clips when any playback-relevant clip or note on that track changes.
- The current clip reuse check is conservative and positional. Track reorders intentionally rebuild.
- Real plugin load/editor/state costs were not measured here.
- Dirty categories are still not explicit. AppServices can still mark playback dirty for changes that may not require any engine work.
- Audio-clip reuse was implemented by equality check, but the probe stressed MIDI clips rather than real audio-file materialization.

## Suggested Next Segment

Run Segment 10 - Automation Editing And Playback Binding.

Reason:
- In-place sync no longer rematerializes unchanged clips.
- Automation-only changes should now be much cheaper at sync time, so the next likely cost is automation editing, binding, and playback lookup itself.

## Performance Segment Handoff

- Segment: 09 - Tracktion Engine Sync And Playback Graph Materialization
- User-visible symptom investigated: delayed playback sync after dirty project edits, especially when edits did not change notes/clips.
- Baseline measurements: 8,192-note mixer-only in-place sync was 459.872 ms; unchanged in-place sync was 416.948 ms.
- Hot paths found: Tracktion MIDI clip materialization and in-place sync rebuilding unchanged clips.
- Changes made: phase-level sync telemetry, sync performance probe, previous-project clip materialization equality checks, track-position-safe clip reuse during in-place sync.
- Files changed: `src/engine/TracktionPlaybackEngine.cpp`, `tests/integration/TracktionSyncPerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, this report.
- Verification run: debug app build, core tests, full test preset, focused integration probes, debug-app sync, and matching executable hash completed.
- Before/after result: 8,192-note mixer-only in-place sync dropped to 2.302 ms; one-note-change sync dropped to 59.565 ms; unchanged sync dropped to 2.642 ms.
- Remaining risks: full sync remains synchronous, one changed track still rematerializes all clips on that track, real plugin costs not measured, dirty categories deferred.
- Suggested next segment: 10 - Automation Editing And Playback Binding.
