# Segment 10 - Automation Editing And Playback Binding

Date: 2026-06-14

## Scope

Measured automation curve editing, command-backed lane replacement, playback snapshot lookup, and timeline automation lane painting.

This segment focused on the core automation model, playback snapshot binding, timeline rendering of visible automation lanes, and command-backed automation lane edits. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

Automation-heavy projects can feel laggy while editing or painting visible lanes. The largest model-side issue was `AutomationCurve::addPoint()`: each added point appended and sorted the entire point vector. Building or replacing a lane with thousands of points therefore became very expensive.

The timeline also copied each visible lane's point vector on every paint, even when no point was being dragged. Dense visible lanes also drew every point as a filled and outlined ellipse, which is costly when many automation points are visible.

## Baseline Measurements

Added an integration performance probe with:

- Curve builds at 100, 1,000, and 5,000 points.
- Playback snapshots with 0, 10, and 100 lanes.
- Command-backed lane replacement at 100, 1,000, and 5,000 points.
- Timeline paints for:
  - 2 tracks, 10 total lanes, 100 points per lane.
  - 4 tracks, 40 total lanes, 100 points per lane.
  - 1 track, 10 total lanes, 1,000 points per lane.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][automation][perf]" 2>&1 \
  | rg "AutomationPerfProbe::|AutomationPlayback::|MixerCommands::SetTrackAutomationLane|TimelineComponent::paint($| automation-lanes| tracks)"
```

Filtered baseline before optimization:

| Probe | Baseline |
|---|---:|
| Build curve, 100 points | 0.411 ms |
| Build curve, 1,000 points | 75.948 ms |
| Build curve, 5,000 points | 1,955.433 ms |
| Snapshot, 0 lanes | 0.053 ms |
| Snapshot, 10 lanes x 100 points | 0.092 ms |
| Snapshot, 100 lanes x 100 points | 0.322 ms |
| Execute `SetTrackAutomationLane`, 100 points | 0.055 ms |
| Execute `SetTrackAutomationLane`, 1,000 points | 0.088 ms |
| Execute `SetTrackAutomationLane`, 5,000 points | 0.271 ms |
| Timeline paint, 10 lanes x 100 points | 150.189 ms |
| Timeline paint, 40 lanes x 100 points | 188.305 ms |
| Timeline paint, 10 lanes x 1,000 points | 239.415 ms |

Phase observations:

- Curve construction was the clearest algorithmic problem.
- Playback snapshot lookup was already sub-millisecond in the synthetic no-plugin probe.
- Timeline automation painting was expensive with many visible points, especially when most points were outside the visible timeline range.
- Automation playback timer was already gated by `automationProjectHasLanes_`, so it is not armed for projects with no automation lanes.

## Changes Made

- Added `tests/integration/AutomationPerformanceProbeTest.cpp` with the tag `[integration][automation][perf]`.
- Added opt-in timers for:
  - `AutomationPlayback::automationPlaybackSnapshotAt`
  - `MixerCommands::SetTrackAutomationLaneCommand::execute`
  - `MixerCommands::SetTrackAutomationLaneCommand::undo`
  - `TimelineComponent::paint automation-lanes track=<id>`
- Changed `AutomationCurve::addPoint()` from append plus full stable-sort to ordered insertion with `std::lower_bound`.
- Changed `AutomationCurve::removePointAt()` from linear search to `std::lower_bound`.
- Optimized `automationPlaybackSnapshotAt()`:
  - Builds a per-snapshot track lookup once.
  - Avoids repeated project-wide track searches for availability and default values.
- Optimized timeline automation paint:
  - Uses the lane's point vector by reference when no point is being dragged.
  - Copies points only for the active drag-preview lane.
  - Culls line and point drawing after the visible timeline end.
  - Draws compact markers for dense visible lanes instead of filled and outlined ellipses for every point.

Files changed:

- `src/core/sequencing/Automation.cpp`
- `src/core/sequencing/AutomationPlayback.cpp`
- `src/core/commands/MixerCommands.cpp`
- `src/ui/TimelineComponent.cpp`
- `tests/integration/AutomationPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/10_AUTOMATION_EDITING_AND_PLAYBACK_BINDING.md`

## Before/After Result

Post-change filtered probe command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][automation][perf]" 2>&1 \
  | rg "AutomationPerfProbe::|AutomationPlayback::|MixerCommands::SetTrackAutomationLane|TimelineComponent::paint($| automation-lanes| tracks)"
```

Post-change timings:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| Build curve, 100 points | 0.411 ms | 0.100 ms | about 76% faster |
| Build curve, 1,000 points | 75.948 ms | 0.528 ms | about 99% faster |
| Build curve, 5,000 points | 1,955.433 ms | 3.106 ms | about 99% faster |
| Snapshot, 10 lanes x 100 points | 0.092 ms | 0.058 ms | about 37% faster |
| Snapshot, 100 lanes x 100 points | 0.322 ms | 0.270 ms | about 16% faster |
| Timeline paint, 10 lanes x 100 points | 150.189 ms | 138.275 ms | about 8% faster |
| Timeline paint, 40 lanes x 100 points | 188.305 ms | 164.184 ms | about 13% faster |
| Timeline paint, 10 lanes x 1,000 points | 239.415 ms | 57.151 ms | about 76% faster |

Command-backed lane replacement stayed sub-millisecond:

| Probe | Before | After |
|---|---:|---:|
| Execute `SetTrackAutomationLane`, 100 points | 0.055 ms | 0.082 ms |
| Execute `SetTrackAutomationLane`, 1,000 points | 0.088 ms | 0.070 ms |
| Execute `SetTrackAutomationLane`, 5,000 points | 0.271 ms | 0.311 ms |

The command timings are small enough that debug-build noise is larger than the behavioral difference. The main editing win is constructing and mutating large curves before the command is executed.

## Hot Paths Found

1. `AutomationCurve::addPoint()` performed a full sort after every insertion.
   - This made imported/generated dense automation lanes extremely expensive to build.

2. Timeline paint copied automation points on every visible lane.
   - Copying is now avoided unless a drag preview needs a temporary edited point list.

3. Timeline paint drew work outside the visible range.
   - The renderer now stops line and marker work after the visible timeline end.

4. Dense point handles were expensive.
   - Dense lanes now use compact point markers while preserving the selected point's larger handle.

5. Snapshot lookup was not the dominant cost.
   - It is now somewhat cheaper and avoids repeated project searches, but it was already not the main source of lag in this probe.

## Verification Run

Completed verification:

- `cmake --build --preset debug --target tsq_engine_integration_tests`
- Filtered `[integration][automation][perf]` probe with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][automation][perf]"`
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][sync][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][device-chain][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-list][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app`.
- Verified matching executable hash:
  - `60e09387b6f2393ae84ac081200bd49120bfa73a87e18cf79afc2e3ef864bd09`

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- `shasum` emitted the known macOS locale warning before printing matching hashes.

## Remaining Risks

- Timeline automation paint can still be expensive when many lanes and many visible points are on screen.
- The current renderer still draws automation lane curves directly every paint; there is no cached geometry/path layer yet.
- Plugin-parameter automation binding against real plugin parameter lists was not measured with real VSTs in this segment.
- Moving a point still replaces the whole automation lane through `SetTrackAutomationLaneCommand`; this is acceptable in the probe but may deserve a point-specific command later.
- Dirty categories are still broad. Automation-only edits still mark the playback project dirty, although Segment 09's clip reuse makes compatible syncs much cheaper.

## Suggested Next Segment

Run Segment 11 - Audio Files, Waveforms, Project Package IO, And Serialization.

Reason:
- Automation curve construction and dense-lane painting are now much cheaper.
- The next likely source of lag is file-heavy work: waveform thumbnails, audio import, package traversal, and save/load serialization.

## Performance Segment Handoff

- Segment: 10 - Automation Editing And Playback Binding
- User-visible symptom investigated: lag from dense automation lanes during creation, editing, playback snapshot lookup, and timeline paint.
- Baseline measurements: 5,000-point curve construction was 1,955.433 ms; 10 lanes x 1,000 points timeline paint was 239.415 ms.
- Hot paths found: full point-vector sort on every point insert, unnecessary point-vector copies during paint, dense offscreen point drawing, dense outlined point handles.
- Changes made: ordered automation point insertion/removal, automation playback track lookup, automation performance probe, no-copy paint path, visible-range culling, dense point markers.
- Files changed: `src/core/sequencing/Automation.cpp`, `src/core/sequencing/AutomationPlayback.cpp`, `src/core/commands/MixerCommands.cpp`, `src/ui/TimelineComponent.cpp`, `tests/integration/AutomationPerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, this report.
- Verification run: debug app build, core tests, full test preset, focused integration probes, debug-app sync, and matching executable hash completed.
- Before/after result: 5,000-point curve construction dropped to 3.106 ms; 10 lanes x 1,000 points timeline paint dropped to 57.151 ms.
- Remaining risks: timeline curve geometry is not cached, real VST parameter binding not measured, point-specific automation commands deferred.
- Suggested next segment: 11 - Audio Files, Waveforms, Project Package IO, And Serialization.
