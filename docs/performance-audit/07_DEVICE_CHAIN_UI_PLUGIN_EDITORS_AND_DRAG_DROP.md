# Segment 07 - Device Chain UI, Plugin Editors, And Drag/Drop

Date: 2026-06-14

## Scope

Measured and reduced Device Chain UI refresh and drag-preview cost.

This segment focused on the lower Device Chain editor: selected-track chain refreshes, card painting, plugin/device drag-over previews, bypass/editor call paths, and device edit sync behavior. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

The Device Chain editor can feel sticky when a track has many devices because every drag move rebuilt preview state for every visible device card and repainted the whole chain. The same component also refreshed eagerly from the lower detail editor's slow idle refresh path when Device Chain mode was visible.

The most important hot path was drag-over preview work. Before this pass, `ChainContentComponent::updateDropPreview()` updated every card on every drag movement, even when only the insert line changed, or when the pointer remained within the same preview region.

## Baseline Measurements

Added an integration performance probe with synthetic selected tracks containing 0, 8, 32, and 96 device slots.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][device-chain][perf]" 2>&1 \
  | rg "DeviceChainPerfProbe::refresh|DeviceChainPerfProbe::paint|DeviceChainPerfProbe::drag"
```

Filtered baseline before optimization:

| Probe | Time |
|---|---:|
| `refresh cold devices=0` | 0.037 ms |
| `refresh warm devices=0` | 0.031 ms |
| `paint devices=0` | 80.754 ms |
| `drag moves devices=0` | 10.186 ms |
| `refresh cold devices=8` | 0.195 ms |
| `refresh warm devices=8` | 0.170 ms |
| `paint devices=8` | 37.992 ms |
| `drag moves devices=8` | 38.902 ms |
| `refresh cold devices=32` | 0.596 ms |
| `refresh warm devices=32` | 0.551 ms |
| `paint devices=32` | 31.925 ms |
| `drag moves devices=32` | 105.543 ms |
| `refresh cold devices=96` | 1.932 ms |
| `refresh warm devices=96` | 1.593 ms |
| `paint devices=96` | 28.000 ms |
| `drag moves devices=96` | 294.065 ms |

The synthetic drag probe sends 240 drag moves across the chain. The 96-device case was spending about 1.2 ms per drag move in debug builds.

## Changes Made

- Added opt-in phase timers for:
  - `DeviceChainComponent::refresh`
  - `DeviceChainComponent::paint`
  - `DeviceChainContent::refreshModel`
  - `DeviceChainContent::paint`
  - `DeviceChainContent::updateDropPreview`
  - `AppServices::assignInstrumentToTrack`
  - `AppServices::addPluginDeviceToTrack`
  - `AppServices::insertPluginDeviceToTrackByStableId`
  - `AppServices::replaceTrackDeviceByStableId`
  - `AppServices::moveTrackDevice`
  - `AppServices::removeTrackDevice`
  - `AppServices::setTrackDeviceBypassed`
  - `AppServices::openTrackPluginEditor`
  - `TracktionPlaybackEngine::openTrackPluginEditor`
  - `TracktionPlaybackEngine::setTrackPluginBypassed`
- Added `tests/integration/DeviceChainPerformanceProbeTest.cpp` with the tag `[integration][device-chain][perf]`.
- Device Chain refresh now fast-exits when the selected track labels, source text, empty text, and slot view models are unchanged and no drop preview is active.
- Device card updates now skip all label/button/repaint work when the card view model and replace-preview state are unchanged.
- Drag preview updates now skip work if the computed empty/insert/replace preview state did not change.
- Drag preview updates now update only the previous and next replacement cards instead of every device card.
- Removed extra repaint calls around drag-enter and drag-exit; preview helpers now repaint only when they actually change visible state.
- Device Chain header labels now update only when their text changes.

Files changed:

- `src/ui/DeviceChainComponent.h`
- `src/ui/DeviceChainComponent.cpp`
- `src/app/AppServices.cpp`
- `src/engine/TracktionPlaybackEngine.cpp`
- `tests/integration/DeviceChainPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/07_DEVICE_CHAIN_UI_PLUGIN_EDITORS_AND_DRAG_DROP.md`

## Before/After Result

Post-change filtered probe command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][device-chain][perf]" 2>&1 \
  | rg "DeviceChainPerfProbe::refresh|DeviceChainPerfProbe::paint|DeviceChainPerfProbe::drag"
```

Post-change timings:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| `drag moves devices=8` | 38.902 ms | 16.844 ms | about 57% faster |
| `drag moves devices=32` | 105.543 ms | 17.780 ms | about 83% faster |
| `drag moves devices=96` | 294.065 ms | 20.728 ms | about 93% faster |
| `refresh warm devices=8` | 0.170 ms | 0.085 ms | about 50% faster |
| `refresh warm devices=32` | 0.551 ms | 0.246 ms | about 55% faster |
| `refresh warm devices=96` | 1.593 ms | 1.091 ms | about 32% faster |

Paint timings stayed noisy because the probe paints the full offscreen component:

| Probe | Before | After | Notes |
|---|---:|---:|---|
| `paint devices=8` | 37.992 ms | 31.655 ms | full offscreen paint |
| `paint devices=32` | 31.925 ms | 29.080 ms | full offscreen paint |
| `paint devices=96` | 28.000 ms | 35.846 ms | noisy debug-build result |

The main win is for drag-over interaction. A 96-device drag sweep dropped from 294.065 ms to 20.728 ms for 240 moves.

## Device Edit Sync Behavior

Device edit paths were inspected and instrumented:

- Append/insert/replace/reorder/remove all execute a command, which marks the playback project dirty through the command-stack change callback.
- Successful append/insert/replace/reorder/remove then call `syncPlaybackAfterDeviceEdit()`, which calls `syncPlaybackProjectIfNeeded()` once.
- `TracktionPlaybackEngine::syncProject()` may choose in-place sync through `canSyncProjectInPlace()`, or it may rebuild the full edit graph. Existing sync phase timers report which path was taken.
- Bypass has an existing fast path:
  - It executes the bypass command.
  - If the playback project was clean and `TracktionPlaybackEngine::setTrackPluginBypassed()` finds the live plugin, it toggles the plugin enabled state without a full project sync.
  - If that fast path is unavailable, it falls back to `syncPlaybackAfterDeviceEdit()`.
- Opening a plugin editor calls `syncPlaybackProjectIfNeeded()` first, then `TracktionPlaybackEngine::openTrackPluginEditor()`.

The synthetic performance probe does not load real third-party plugins, so it does not claim real-world plugin load/editor timings. The new timers make those paths visible during hands-on plugin QA.

## Hot Paths Found

1. Drag preview updated every card on every move.
   - The new path updates only when preview state changes and only touches affected replacement cards.

2. Warm Device Chain refresh rebuilt view models and repainted even when the selected track was unchanged.
   - The new path skips repeated model/card work when the view state is identical.

3. Device edit sync behavior depends on edit type.
   - Structural device edits still require a playback sync.
   - Bypass can avoid sync when the live plugin is already present and the playback project was clean.

4. Full offscreen paint remains measurable but was not the main user-facing drag bottleneck in this segment.

## Verification Run

Verification completed:

- `cmake --build --preset debug --target tsq_engine_integration_tests`
- Filtered `[integration][device-chain][perf]` probe with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][device-chain][perf]"`
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][device-chain][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-list][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app` and verified matching binary hashes.

Result:

- Debug app target rebuilt successfully.
- Core test target built and `ctest --preset tests` passed.
- Focused integration target rebuilt successfully.
- Device Chain performance probe passed.
- Track List performance probe passed.
- Focused non-VST meter integration passed.
- Focused non-VST package/import integration passed.
- Both debug app bundle paths contain the same binary hash: `b9dc9c0672707cdae3b1ba3e833ed9c4706b035219e96b4cd5e2fbbfd60b8b72`.

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- During the first debug rebuild, several generated `.a` outputs contained Git LFS pointer text. During the first tests-preset rebuild, the matching generated tests-preset archives had the same issue. They were removed with `cmake -E rm -f` and regenerated successfully by Ninja.
- Locale warnings from `shasum` because the shell environment uses `C.UTF-8`, which this macOS Perl build does not support.

## Remaining Risks

- Real plugin insert/replace/editor timings were not measured because the synthetic probe uses fake plugin descriptions and device slots.
- Structural device edits still call playback sync. Segment 09 should decide how much of that can become incremental.
- Bypass fast path depends on a clean playback project and a live plugin lookup succeeding. Dirty projects or missing live plugins still fall back to sync.
- Full offscreen paint remains noisy in debug builds; viewport-clipped paint should be inspected if large chains still feel slow.
- Device reorder/drop behavior should get hands-on QA with real plugin chains, especially around open editors and saved state.

## Suggested Next Segment

Run Segment 08 - AppServices Commands, Dirtying, Undo/Redo, And Project Mutation.

Reason:
- Device Chain UI churn is much lower.
- The remaining costly work is now mostly mutation/sync policy: which edits dirty playback, when sync runs, and whether command execution snapshots are doing too much.

## Performance Segment Handoff

- Segment: 07 - Device Chain UI, Plugin Editors, And Drag/Drop
- User-visible symptom investigated: sticky drag/drop and repeated refresh work in large device chains.
- Baseline measurements: 96-device drag sweep was 294.065 ms; 32-device drag sweep was 105.543 ms; 96-device warm refresh was 1.593 ms.
- Hot paths found: every drag move updated every card, unchanged refreshes still rebuilt card view state, structural edits run playback sync, bypass has a conditional fast path.
- Changes made: device-chain performance probe/timers, refresh fast-exit, card update skip, preview-state skip, affected-card-only replace preview updates, service/engine device-path timers.
- Files changed: `src/ui/DeviceChainComponent.*`, `src/app/AppServices.cpp`, `src/engine/TracktionPlaybackEngine.cpp`, `tests/integration/DeviceChainPerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, this report.
- Verification run: debug app build, core tests, device-chain performance probe, track-list performance probe, meter integration, package/import integration, and debug bundle sync/hash check.
- Before/after result: 96-device drag sweep dropped from 294.065 ms to 20.728 ms; 32-device drag sweep dropped from 105.543 ms to 17.780 ms.
- Remaining risks: real plugin load/editor timings not measured, structural device edits still sync playback, bypass fast path depends on live clean project state.
- Suggested next segment: 08 - AppServices Commands, Dirtying, Undo/Redo, And Project Mutation.
