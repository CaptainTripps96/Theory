# Segment 06 - Track Headers, Mixer UI, Meters, Routing, And Sends

Date: 2026-06-11

## Scope

Measured and reduced Track List refresh cost for mixer-style track headers.

This segment focused on the left track-header/mixer rows: meter polling, row repaint behavior, routing and send dropdown construction, slider commit behavior, and layout work with many tracks. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

The app can feel very laggy with many tracks because `MainComponent::timerCallback()` refreshes the Track List while playback is active and also refreshes it periodically while stopped. Before this pass, a warm Track List refresh rebuilt every visible row's routing and send dropdown choices, even when no track model or routing topology changed.

The worst hot path was route-choice validation. Each row generated Audio To, Audio From, MIDI To, MIDI From, and send choices; each candidate route copied the project and called `validateProjectRouting()`. With many tracks, that made a repeated UI refresh scale extremely poorly.

## Baseline Measurements

Added an integration performance probe with synthetic projects containing 1, 16, 64, and 128 tracks.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-list][perf]"
```

Filtered baseline before optimization:

| Probe | Time |
|---|---:|
| `TrackListPerfProbe::refresh cold tracks=1` | 58.335 ms |
| `TrackListPerfProbe::refresh warm tracks=1` | 0.322 ms |
| `TrackListPerfProbe::paint tracks=1` | 15.017 ms |
| `TrackListPerfProbe::refresh cold tracks=16` | 40.606 ms |
| `TrackListPerfProbe::refresh warm tracks=16` | 20.776 ms |
| `TrackListPerfProbe::paint tracks=16` | 66.136 ms |
| `TrackListPerfProbe::refresh cold tracks=64` | 926.216 ms |
| `TrackListPerfProbe::refresh warm tracks=64` | 800.788 ms |
| `TrackListPerfProbe::paint tracks=64` | 360.068 ms |
| `TrackListPerfProbe::refresh cold tracks=128` | 5814.024 ms |
| `TrackListPerfProbe::refresh warm tracks=128` | 5289.382 ms |
| `TrackListPerfProbe::paint tracks=128` | 757.979 ms |

Hot phases:

- `TrackListComponent::refresh updateRows` dominated warm refreshes.
- `TrackHeaderComponent::update routing-controls` dominated row updates.
- `TrackHeaderComponent::update send-controls` was also measurable.
- `TrackListComponent::refresh getMeterSnapshot` was only microseconds in the stopped synthetic probe.

## Changes Made

- Added opt-in phase timers for:
  - `TrackListComponent::refresh`
  - `TrackListComponent::refresh rebuildRowsIfNeeded`
  - `TrackListComponent::refresh getMeterSnapshot`
  - `TrackListComponent::refresh updateRows`
  - `TrackHeaderComponent::updateFromTrack`
  - `TrackHeaderComponent::update routing-controls`
  - `TrackHeaderComponent::update send-controls`
  - `TrackHeaderComponent::paint`
- Added `tests/integration/TrackListPerformanceProbeTest.cpp` with the tag `[integration][track-list][perf]`.
- Track List refresh now builds a per-refresh map from track id to meter snapshot instead of linearly searching meter sources for each row.
- Track List refresh now computes one routing-topology fingerprint per refresh.
- Track header rows now keep a cached model fingerprint and routing-topology fingerprint.
- Unchanged rows now skip the expensive full model update and route/send dropdown rebuild.
- Meter repainting is now selective:
  - Selection or drop-highlight changes repaint the row.
  - Material meter changes repaint the meter bounds.
  - Identical refreshes do not repaint the row.
- Track List no longer repaints the whole container after every refresh unless rows were rebuilt or relaid out.
- Track List row layout now uses a layout fingerprint and an O(n) layout pass.
- Track hit testing now walks row positions incrementally instead of repeatedly recomputing prior row heights.
- Volume, pan, and send sliders were inspected and already commit command-stack changes at drag end; non-drag value changes still commit immediately for direct click/text-style interactions.

Files changed:

- `src/ui/TrackListComponent.h`
- `src/ui/TrackListComponent.cpp`
- `tests/integration/TrackListPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/06_TRACK_HEADERS_MIXER_UI_METERS_ROUTING_AND_SENDS.md`

## Before/After Result

Post-change filtered probe command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-list][perf]" 2>&1 \
  | rg "TrackListPerfProbe::refresh|TrackListPerfProbe::paint|TrackListComponent::refresh updateRows"
```

Post-change timings:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| `refresh warm tracks=1` | 0.322 ms | 0.074 ms | about 77% faster |
| `refresh warm tracks=16` | 20.776 ms | 0.332 ms | about 98% faster |
| `refresh warm tracks=64` | 800.788 ms | 0.860 ms | about 99.9% faster |
| `refresh warm tracks=128` | 5289.382 ms | 1.870 ms | about 99.96% faster |

Cold refresh and full offscreen paint remained expensive:

| Probe | Before | After | Notes |
|---|---:|---:|---|
| `refresh cold tracks=64` | 926.216 ms | 909.928 ms | still does full routing/send construction |
| `refresh cold tracks=128` | 5814.024 ms | 5934.403 ms | still does full routing/send construction |
| `paint tracks=64` | 360.068 ms | 304.681 ms | full offscreen paint of all rows |
| `paint tracks=128` | 757.979 ms | 809.207 ms | full offscreen paint of all rows; noisy debug-build result |

Important phase change:

| Phase | Before | After |
|---|---:|---:|
| `TrackListComponent::refresh updateRows`, 128-track warm refresh | 5289.155 ms | 1.660 ms |

The main win is for repeated timer-driven refreshes while playback is active or while the app is idle. Those refreshes now avoid rebuilding routing and send controls unless track model state or routing topology actually changed.

## Hot Paths Found

1. Routing and send dropdown construction was the critical repeated cost.
   - The route validation helper copies the project and validates it per candidate route.
   - Before this pass, every timer refresh paid that cost for every row.

2. Whole-container repainting was too broad for meter updates.
   - Rows now repaint only when their visual state materially changes.

3. Meter lookup was an avoidable repeated linear search.
   - Track List now builds a local lookup map once per refresh.

4. Row layout and hit testing had repeated prior-row height scans.
   - Layout and drag hit testing now walk row positions incrementally.

## Verification Run

Verification completed:

- `cmake --build --preset debug --target tsq_engine_integration_tests`
- Filtered `[integration][track-list][perf]` probe with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-list][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app` and verified matching binary hashes.

Result:

- Debug app target rebuilt successfully.
- Core test target built and `ctest --preset tests` passed.
- Focused integration target rebuilt successfully.
- Track List performance probe passed.
- Focused non-VST meter integration passed.
- Focused non-VST package/import integration passed.
- Both debug app bundle paths contain the same binary hash: `5d2650af1abdb34dc4c13b72be362137951bd078d72b5c19efbfe636be3a8ea6`.

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- Locale warnings from `shasum` because the shell environment uses `C.UTF-8`, which this macOS Perl build does not support.

## Remaining Risks

- The first full refresh after a routing topology change is still very expensive at high track counts because every row rebuilds route/send choices.
- A deeper routing-menu pass should replace per-candidate project copy/validation with a cheaper topology graph or an owner-level route choice cache.
- The synthetic probe measured stopped-engine meter snapshots. Playback with active meter data should be profiled separately.
- Full offscreen row painting is still costly in debug builds, especially at 64-128 rows. Real viewport clipping may reduce user-visible cost, but this deserves a dedicated paint/visibility pass if large sessions still feel slow.
- Slider drag command count was inspected by code and appears safe, but it was not instrumented with a direct command-count probe.

## Suggested Next Segment

Run Segment 07 - Device Chain UI, Plugin Editors, And Drag/Drop.

Reason:
- Timer-driven Track List refreshes are now much cheaper.
- Device-chain interactions are the next likely source of UI churn and engine rebuilds, especially around plugin drag/drop, bypass, editor lifecycle, and plugin-state capture.

## Performance Segment Handoff

- Segment: 06 - Track Headers, Mixer UI, Meters, Routing, And Sends
- User-visible symptom investigated: lag from timer-driven Track List refreshes and mixer row/meter updates.
- Baseline measurements: 128-track warm refresh was 5289.382 ms; 64-track warm refresh was 800.788 ms.
- Hot paths found: route/send dropdown construction, per-candidate route validation through project copies, whole-container repaint, linear meter lookup, O(n²)-ish row layout/hit-test helpers.
- Changes made: track-header model fingerprints, shared routing-topology fingerprint, selective row/meter repaint, meter lookup map, row layout fingerprint, O(n) layout and hit testing, performance probe/timers.
- Files changed: `src/ui/TrackListComponent.*`, `tests/integration/TrackListPerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, this report.
- Verification run: debug app build, core tests, track-list performance probe, meter integration, package/import integration, and debug bundle sync/hash check.
- Before/after result: 128-track warm refresh dropped from 5289.382 ms to 1.870 ms; 64-track warm refresh dropped from 800.788 ms to 0.860 ms.
- Remaining risks: cold topology-change refresh remains expensive, active playback meter data needs separate profiling, full offscreen paint remains costly in debug builds.
- Suggested next segment: 07 - Device Chain UI, Plugin Editors, And Drag/Drop.
