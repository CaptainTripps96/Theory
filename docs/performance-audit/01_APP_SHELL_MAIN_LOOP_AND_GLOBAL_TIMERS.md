# Segment 01 - App Shell, Main Message Loop, And Global Timers

Date: 2026-06-10

## Scope

Measured and reduced idle message-thread churn from the main app timer.

This segment focused on `MainComponent::timerCallback()` because Segment 00 showed the default project repainting Timeline repeatedly while idle. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

The whole app feels laggy, even before isolating a specific editor. A global 24 Hz timer can make every surface feel slow if it forces expensive repaints while nothing is changing.

## Baseline Measurements

Command:

```sh
rm -f /tmp/tsq_perf_segment01_before.log /tmp/tsq_perf_segment01_before.pid
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer \
  > /tmp/tsq_perf_segment01_before.log 2>&1 &
echo $! > /tmp/tsq_perf_segment01_before.pid
sleep 8
kill $(cat /tmp/tsq_perf_segment01_before.pid) 2>/dev/null || true
wait $(cat /tmp/tsq_perf_segment01_before.pid) 2>/dev/null || true
```

Before the timer change:

| Label | Samples | Avg | Max |
|---|---:|---:|---:|
| `AppServices startup body` | 1 | 3303.187 ms | 3303.187 ms |
| `TimelineComponent::paint` | 34 | 6.691 ms | 14.574 ms |
| `Logger::log` | 11 | 0.584 ms | 4.945 ms |
| `TrackListComponent::paint` | 35 | 1.050 ms | 4.531 ms |
| `MainComponent constructor` | 1 | 2.086 ms | 2.086 ms |
| `MainComponent::timerCallback` | 35 | 0.758 ms | 1.732 ms |
| `TrackListComponent::refresh` | 35 | 0.539 ms | 1.469 ms |
| `PianoRollComponent::paint` | 34 | 0.427 ms | 1.182 ms |
| `TransportComponent::timerCallback` | 3 | 0.143 ms | 0.188 ms |
| `DetailEditorComponent::refresh` | 35 | 0.008 ms | 0.024 ms |

Trace/log volume:
- Process trace: 247 lines.
- Diagnostics log: 11 lines.

Interpretation:
- `MainComponent::timerCallback()` unconditionally called:
  - `timelineComponent_.repaint()`
  - `trackListComponent_.refresh()`
  - `detailEditorComponent_.refresh()`
- `TimelineComponent::setPlayheadTick()` already repaints Timeline when the playhead actually changes.
- `DetailEditorComponent::setPlayheadTick()` already propagates playhead changes to the Piano Roll.
- The unconditional idle repaint path was redundant while stopped.

## Changes Made

Changed the main timer from unconditional refresh/repaint to state-aware refresh:

- Main timer still runs at 24 Hz.
- MIDI recording events and live plugin parameter observation still run each tick.
- Playhead polling and playhead-driven repaints now run at full timer rate only while playback or MIDI recording is active.
- Track List refresh now runs every tick during playback so meters remain responsive.
- While stopped/idle, Timeline repaint, Track List refresh, and Detail Editor refresh run only every 12 timer ticks as a low-frequency safety refresh.
- App-level undo, redo, and playback toggle now explicitly refresh visible project surfaces instead of relying on the timer's unconditional behavior.

Files changed:
- `src/ui/MainComponent.h`
- `src/ui/MainComponent.cpp`

## Before/After Result

After the timer change:

Command:

```sh
rm -f /tmp/tsq_perf_segment01_after.log /tmp/tsq_perf_segment01_after.pid
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer \
  > /tmp/tsq_perf_segment01_after.log 2>&1 &
echo $! > /tmp/tsq_perf_segment01_after.pid
sleep 8
kill $(cat /tmp/tsq_perf_segment01_after.pid) 2>/dev/null || true
wait $(cat /tmp/tsq_perf_segment01_after.pid) 2>/dev/null || true
```

| Label | Samples | Avg | Max |
|---|---:|---:|---:|
| `AppServices startup body` | 1 | 3303.634 ms | 3303.634 ms |
| `TimelineComponent::paint` | 4 | 8.469 ms | 14.193 ms |
| `TrackListComponent::paint` | 4 | 1.701 ms | 4.605 ms |
| `MainComponent::timerCallback` | 33 | 0.113 ms | 2.083 ms |
| `MainComponent constructor` | 1 | 2.063 ms | 2.063 ms |
| `PianoRollComponent::paint` | 4 | 0.737 ms | 1.878 ms |
| `TrackListComponent::refresh` | 2 | 1.094 ms | 1.813 ms |
| `Logger::log` | 11 | 0.150 ms | 0.269 ms |
| `TransportComponent::timerCallback` | 3 | 0.195 ms | 0.227 ms |
| `DetailEditorComponent::refresh` | 2 | 0.007 ms | 0.008 ms |

Trace/log volume:
- Process trace: 88 lines.
- Diagnostics log: 11 lines.

Delta:

| Measurement | Before | After | Change |
|---|---:|---:|---:|
| `TimelineComponent::paint` samples | 34 | 4 | -88% |
| `TrackListComponent::refresh` samples | 35 | 2 | -94% |
| `DetailEditorComponent::refresh` samples | 35 | 2 | -94% |
| `MainComponent::timerCallback` avg | 0.758 ms | 0.113 ms | -85% |
| Process trace lines | 247 | 88 | -64% |

## Hot Paths Found

1. Idle Timeline repaint frequency was the primary shared-loop issue.
   - The individual Timeline paint remains expensive, but the app no longer schedules it continuously while stopped.

2. Track List refresh was also running continuously while stopped.
   - This repeatedly pulled meter snapshots and updated row controls even when meters were silent.

3. The timer callback itself is now small most ticks.
   - The max sample after the change is from slow idle refresh ticks, which is expected.

## Verification Run

- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app` and verified matching binary hashes.

Result:
- Debug app target rebuilt successfully.
- Core tests passed.
- Focused non-VST meter integration passed.
- Focused non-VST package/import integration passed.
- Both debug app bundle paths now contain the same binary.

## Remaining Risks

- This segment measured stopped/idle behavior only.
- Playback meter smoothness still needs manual observation because Track List refresh remains full-rate only during playback.
- The slow idle refresh is a conservative fallback. A later event/listener model could remove it entirely.
- `TimelineComponent::paint` is still expensive when it runs; Segment 03 should inspect grid/clip/waveform paint work.
- Startup remains around 3.3 seconds in these traces; startup should be split in a later dedicated pass if launch time matters.

## Suggested Next Segment

Run Segment 04 - Piano Roll Rendering, Lanes, And Note Editing, or Segment 03 - Timeline Arrangement Rendering And Interaction.

Recommended next choice: Segment 03.

Reason:
- Segment 01 reduced idle paint frequency, but Timeline paint itself is still the largest recurring UI cost when it happens.
