# Segment 13 - MIDI Input, Recording, Transport, And Playhead Polling

Date: 2026-06-14

## Scope

Measured MIDI/transport-adjacent UI work: piano-roll playhead updates, timeline playhead updates, return-to-zero, transport status refresh, and hidden detail-editor playhead forwarding.

This segment did not run the expensive Synthesizer/VST parameter-wipe stress harness. MIDI input callback code was inspected, but the code change targeted repeatable UI polling/playhead hot paths rather than live hardware MIDI input.

## User-Visible Symptom Investigated

Playback and editing can feel laggy when the playhead is moving because the main timer polls the engine and forwards playhead ticks to the timeline and lower editor. The piano roll was treating every playhead move like a geometry-changing edit, which rebuilt content bounds, toolbar clip controls, grid controls, and harmonic note-lane layout.

## Baseline Measurements

Added `tests/integration/MidiTransportPerformanceProbeTest.cpp` with tag `[integration][midi-transport][perf]`.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][midi-transport][perf]"
```

Filtered baseline before optimization:

| Probe | Baseline |
|---|---:|
| Piano-roll playhead moves, 4096 updates | 3,857.124 ms |
| Timeline playhead moves, 4096 updates | 0.469 ms |
| Return to zero | 0.092 ms |

The baseline trace showed repeated `PianoRollContent::visiblePitchSegments` and `visiblePitchLanesForSegment` work while only the playhead cursor was moving.

## Changes Made

- Added `[integration][midi-transport][perf]` probe for piano-roll playhead movement, timeline playhead movement, and return-to-zero.
- Changed `PianoRollComponent::setPlayheadTick()` so playhead movement no longer calls `refreshContentBounds()`.
- Added piano-roll playhead dirty-rectangle repainting for only the old/new cursor strips.
- Added a cached piano-roll playhead layout keyed by selected clip, clip start/length/source length, zoom, and key/scale region geometry.
- Changed timeline playhead updates to repaint only the old/new cursor strips.
- Added cached timeline playhead X coordinate and invalidation when timeline geometry changes.
- Changed `DetailEditorComponent` to remember playhead ticks but skip hidden piano-roll updates while the lower editor is showing the device chain.
- Changed `TransportComponent` to avoid rebuilding the MIDI input combo box when the available device list and selection have not changed.

Files changed:

- `src/ui/PianoRollComponent.cpp`
- `src/ui/TimelineComponent.cpp`
- `src/ui/TimelineComponent.h`
- `src/ui/DetailEditorComponent.cpp`
- `src/ui/DetailEditorComponent.h`
- `src/ui/TransportComponent.cpp`
- `src/ui/TransportComponent.h`
- `tests/integration/MidiTransportPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/13_MIDI_INPUT_RECORDING_TRANSPORT_AND_PLAYHEAD_POLLING.md`

## Before/After Result

Post-change focused probe:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| Piano-roll playhead moves, 4096 updates | 3,857.124 ms | 11.263 ms | about 99.7% faster |
| Timeline playhead moves, 4096 updates | 0.469 ms | 24.873 ms | higher scheduling cost, much smaller repaint region |
| Return to zero | 0.092 ms | 0.053 ms | about the same/noisy |

Timeline playhead scheduling now does a little more math per call so it can invalidate only the cursor strips instead of the whole timeline. The probe measures method cost, not paint cost; this trade is expected to help real playback UI redraws where full timeline repaint area is the expensive part.

## Hot Paths Found

1. Piano-roll playhead movement rebuilt content geometry.
   - Fixed by separating cursor movement from content sizing and control refresh.

2. Piano-roll cursor repaint rebuilt harmonic segment layout repeatedly.
   - Fixed with a playhead-layout cache.

3. Timeline playhead movement invalidated the entire arrangement view.
   - Fixed by invalidating only old/new cursor strips.

4. Hidden piano roll still received playhead movement while device-chain mode was active.
   - Fixed by coalescing ticks in `DetailEditorComponent`.

5. Transport status refresh rebuilt the MIDI input combo every timer tick.
   - Fixed with a simple device-list fingerprint.

## MIDI Recording Notes

`MidiInputRecordingService` already keeps the device callback path allocation-free and lock-free by pushing note-on/off primitives into a fixed `juce::AbstractFifo`. App-side recording still converts note-off events into normal commands one note at a time, which is behaviorally correct but may become a dense-recording bottleneck later.

Deferred recording-specific candidates:

- Batch note-add mutations for dense live recording.
- Add a deterministic MIDI-input injection seam for stress testing without real hardware.
- Surface dropped-event warnings from the message thread without logging from the MIDI callback.

## Verification Run

Completed verification:

- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- Source-only whitespace check: `git diff --check -- src tests docs`
- Filtered `[integration][midi-transport][perf]` probe with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][midi-transport][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][piano-roll][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][audio-package][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][automation][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][sync][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app`.
- Verified matching executable hash:
  - `5411420a0caae87c30144c8a1c525793d5dd9aaf81545a5ac18c09a0a6696556`

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- JUCE assertion in `juce_Component.cpp:3022` in piano-roll integration paint setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- Full `git diff --check` still reports generated CMake log whitespace under `out/build`; source/doc/test diff check is clean.
- `shasum` emitted the known macOS locale warning before printing matching hashes.

## Remaining Risks

- Timeline playhead method scheduling is slightly higher because it computes narrow dirty regions; real paint workload should be lower, but a future dirty-region paint probe would verify this directly.
- Live MIDI recording stress was inspected but not deeply optimized in this segment.
- The piano-roll playhead-layout cache invalidates on key/scale region geometry and clip timing changes; note-only changes that affect accidental lanes do not need to move the playhead X position, so they intentionally do not invalidate this cursor-layout cache.
- Transport MIDI device polling still calls `juce::MidiInput::getAvailableDevices()` every status refresh; only UI combo mutation is cached.

## Suggested Next Segment

Run Segment 14 - Project Save/Load, Autosave, And Diagnostics IO.

Reason:

- Playhead polling now avoids the largest piano-roll UI churn.
- Remaining lag risk moves toward synchronous IO, project persistence, diagnostics, and save/load paths.

## Performance Segment Handoff

- Segment: 13 - MIDI Input, Recording, Transport, And Playhead Polling
- User-visible symptom investigated: playback/playhead movement causing piano-roll and timeline UI lag.
- Baseline measurements: piano-roll 4096 playhead moves took 3,857.124 ms; timeline 4096 playhead moves took 0.469 ms; return-to-zero took 0.092 ms.
- Hot paths found: piano-roll playhead movement rebuilt geometry and harmonic lanes; timeline invalidated the full arrangement; hidden piano roll received ticks in device-chain mode; transport rebuilt MIDI combo items every timer tick.
- Changes made: playhead-only piano-roll invalidation, cached piano-roll playhead layout, narrow timeline playhead repaint, cached timeline playhead X, hidden-editor coalescing, MIDI input combo fingerprint cache, new MIDI/transport perf probe.
- Files changed: `src/ui/PianoRollComponent.cpp`, `src/ui/TimelineComponent.*`, `src/ui/DetailEditorComponent.*`, `src/ui/TransportComponent.*`, `tests/integration/MidiTransportPerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, this report.
- Verification run: debug app build, core tests, source-only diff check, focused Segment 13 probe, piano-roll/timeline/automation/sync/meter/commands integrations, debug app sync, matching executable hash.
- Before/after result: piano-roll playhead movement dropped from 3,857.124 ms to 11.263 ms for 4096 updates.
- Remaining risks: live MIDI recording stress still needs a deterministic injection seam; timeline dirty-region benefit should be verified with a future paint-region probe; MIDI device enumeration is still polled.
- Suggested next segment: 14 - Project Save/Load, Autosave, And Diagnostics IO.
