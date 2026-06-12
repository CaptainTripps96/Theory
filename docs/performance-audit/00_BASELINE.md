# Segment 00 - Baseline And Instrumentation Harness

Date: 2026-06-10

## Scope

Started the system-wide performance audit with opt-in timing instrumentation and a short cold-launch/default-project idle run.

This pass did not attempt deep interactive measurement yet. It launched the debug app, allowed the default workspace to idle briefly, then terminated the process. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## Instrumentation Added

Added opt-in scoped performance tracing:

- `src/core/diagnostics/PerformanceTrace.h`
- `src/core/diagnostics/PerformanceTrace.cpp`

Environment variables:

- `TSQ_PERF_TRACE=1` enables trace output.
- `TSQ_PERF_THRESHOLD_US=<microseconds>` logs only timings at or above the threshold.
- `TSQ_PERF_THRESHOLD_MS=<milliseconds>` is an alternate threshold form.
- If no threshold is set, the default threshold is 1000 microseconds.

Segment 00 timing hooks were added to:

- `Logger::log`
- `AppServices` startup body, plugin-state tracing, playback sync, dirty marking, and deferred restore scheduling
- `TracktionPlaybackEngine::syncProject` and key full-sync phases
- `MainComponent` constructor and timer callback
- `TimelineComponent::paint`
- `PianoRollComponent::paint`
- `TrackListComponent::paint` and `TrackListComponent::refresh`
- `DetailEditorComponent::refresh`
- `BrowserPanelComponent::timerCallback`
- `TransportComponent::timerCallback`

The trace is off by default.

## Command Run

```sh
cmake --build --preset debug --target tsq_app

rm -f /tmp/tsq_perf_segment00.log /tmp/tsq_perf_segment00.pid
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer \
  > /tmp/tsq_perf_segment00.log 2>&1 &
echo $! > /tmp/tsq_perf_segment00.pid
sleep 8
kill $(cat /tmp/tsq_perf_segment00.pid) 2>/dev/null || true
wait $(cat /tmp/tsq_perf_segment00.pid) 2>/dev/null || true
```

## Baseline Measurements

Trace file:
- `/tmp/tsq_perf_segment00.log`
- 344 total captured lines.

Diagnostics log:
- `~/Library/Application Support/TheorySequencer/diagnostics.log`
- 11 lines, 4 KB.
- This idle launch did not emit plugin-state trace spam; it logged startup/audio/plugin-registry basics only.

Timing summary:

| Label | Samples | Avg | Max |
|---|---:|---:|---:|
| `AppServices startup body` | 1 | 3110.013 ms | 3110.013 ms |
| `TimelineComponent::paint` | 51 | 6.133 ms | 13.876 ms |
| `TrackListComponent::paint` | 51 | 1.045 ms | 4.672 ms |
| `MainComponent constructor` | 1 | 2.143 ms | 2.143 ms |
| `Logger::log` | 11 | 0.282 ms | 1.575 ms |
| `MainComponent::timerCallback` | 50 | 0.741 ms | 1.344 ms |
| `TrackListComponent::refresh` | 50 | 0.531 ms | 1.141 ms |
| `PianoRollComponent::paint` | 51 | 0.414 ms | 1.068 ms |
| `TransportComponent::timerCallback` | 5 | 0.108 ms | 0.131 ms |
| `DetailEditorComponent::refresh` | 50 | 0.009 ms | 0.050 ms |

Slowest individual samples:

| Timing | Label |
|---:|---|
| 3110.013 ms | `AppServices startup body` |
| 13.876 ms | `TimelineComponent::paint` |
| 10.875 ms | `TimelineComponent::paint` |
| 7.322 ms | `TimelineComponent::paint` |
| 7.092 ms | `TimelineComponent::paint` |
| 6.994 ms | `TimelineComponent::paint` |

Other observations:
- App-enabled startup still prints the existing `JUCE Assertion failure in juce_AudioPluginFormatManager.cpp:79`.
- The recurring idle cost is dominated by Timeline painting, not by Track List, Piano Roll, or Transport in this default-project idle run.
- `MainComponent::timerCallback` still calls `timelineComponent_.repaint()`, `trackListComponent_.refresh()`, and `detailEditorComponent_.refresh()` every tick.

## Hot Paths Found

1. `TimelineComponent::paint` is the largest recurring idle cost.
   - Even the default project produced 51 Timeline paint samples in the short run.
   - Average Timeline paint was about 6.1 ms, with a 13.9 ms max.
   - This is suspicious because the default project should not require heavy repainting while idle.

2. `MainComponent::timerCallback` is a likely shared lag amplifier.
   - It unconditionally repaints Timeline and refreshes Track List / Detail Editor.
   - Even if the timer callback itself is under 1 ms on average, it schedules more expensive component paints.

3. Startup is still slow at the `AppServices` body level.
   - This coarse timer includes logging, default project setup, plugin-registry load, Tracktion playback engine initialization, and audio output restore.
   - A later pass should split startup into phases if launch lag is a priority.

4. Logger cost is visible but not yet proven to be the primary idle issue.
   - 11 startup log calls averaged 0.282 ms and maxed at 1.575 ms.
   - Segment 02 still needs an editing-session trace, because VST/plugin-state debug logging may be much heavier during note creation and playback actions.

## Changes Made

- Added opt-in performance trace infrastructure.
- Added coarse Segment 00 timing hooks.
- Created this baseline note.

## Verification

Build:
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`

Result:
- Debug app target built successfully.
- Core tests passed.
- Focused non-VST package/import integration passed.
- Existing third-party no-symbol archive warnings appeared.
- Existing duplicate `tsq_core` linker warning appeared.

## Remaining Risks

- This baseline did not include interactive note entry, clip dragging, playback, plugin editor usage, or a large project.
- `TSQ_PERF_THRESHOLD_US=0` is intentionally noisy and should not be used for normal testing.
- The timing hooks use console output when enabled, so enabled tracing can itself perturb very small timings.
- Paint timing from a short app launch is enough to identify suspects, but not enough to rank all lag sources conclusively.

## Suggested Next Segment

Run Segment 02 - Diagnostics, Logging, And VST Watchdog Verbosity.

Reason:
- The user reports broad lagginess, and this codebase currently has verbose VST/plugin-state tracing around piano-roll and playback actions.
- Segment 00 already shows Timeline/Main timer pressure, but Segment 02 can confirm whether the recent debug tracing is compounding lag during actual editing.
