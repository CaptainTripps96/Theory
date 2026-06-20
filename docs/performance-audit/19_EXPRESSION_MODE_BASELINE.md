# Segment 19 - Expression Mode Baseline

Date: 2026-06-15

## Scope

Created the first Expression Mode baseline before adding the expression data model or UI.

This segment deliberately did **not** implement Expression Mode. It added repeatable probes for the paths that Expression Mode will stress:

- Piano-roll paint with small, medium, and dense MIDI clips.
- Piano-roll select-all.
- Piano-roll marquee selection through the same selection code used by real mouse drags.
- Tracktion playback sync with a first-party `Simple Osc Complex` instrument on a MIDI track.
- Simple Osc Complex deterministic render behavior.

## Why This Matters

Expression Mode will add lane overlays, release ghost notes, phrase marquee selection, dense expression curves, and first-party synth modulation. Without a baseline, it would be too easy to make the editor feel slower or to blur audio-path responsibility.

The big rule remains:

```text
Pristine audio path is king.
Performance is a very close second.
```

## Changes Made

- Added `tests/integration/ExpressionBaselinePerformanceProbeTest.cpp`.
- Added `PianoRollComponent::debugEmulateMarqueeSelectAllVisibleNotes()` so tests can measure the actual marquee selection path.
- Added a deterministic render test for `Simple Osc Complex`.
- Added the new integration probe to `tests/CMakeLists.txt`.

Files changed:

- `src/ui/PianoRollComponent.h`
- `src/ui/PianoRollComponent.cpp`
- `tests/core/SimpleOscComplexSynthTest.cpp`
- `tests/integration/ExpressionBaselinePerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- this report

## Probe Command

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  out/build/debug/tests/tsq_engine_integration_tests "[integration][expression][baseline][perf]" 2>&1 \
  | tee /tmp/expression-baseline-perf-rerun.log
```

Focused synth test:

```sh
out/build/debug/tests/tsq_core_tests "[devices][simple-osc-complex]"
```

## Baseline Results

Final rerun values:

| Probe | 64 Notes | 512 Notes | 2,048 Notes |
|---|---:|---:|---:|
| Piano-roll cold paint | 53.714 ms | 49.224 ms | 52.158 ms |
| Piano-roll warm paint | 12.422 ms | 16.339 ms | 31.946 ms |
| Piano-roll select all | 0.044 ms | 0.203 ms | 1.156 ms |
| Piano-roll marquee visible notes | 1.455 ms | 18.038 ms | 173.222 ms |
| Simple Osc native sync | 11.833 ms | 37.909 ms | 118.894 ms |

## Hot Paths Found

1. Marquee selection is the clearest baseline risk.
   - 2,048-note visible marquee selection took 173.222 ms.
   - Expression Mode release ghosts and phrase selection must not multiply this cost.
   - Future prompts should optimize note/ghost hit-testing before making marquee selection more complex.

2. Warm piano-roll paint scales with visible note count.
   - 2,048-note warm paint took 31.946 ms.
   - Expression overlays need culling and cached geometry from the start.

3. Cold piano-roll paint is dominated by lane-cache construction.
   - Cold paint hovered around 49-54 ms in this run.
   - This is expected because the lane background cache is built during the first paint.

4. Simple Osc sync scales mostly with Tracktion MIDI clip materialization.
   - 2,048-note native synth sync took 118.894 ms.
   - Segment 09 already identified clip materialization as the core sync cost.
   - Expression playback preparation must be incremental and should not force full clip rematerialization for display-only edits.

5. Select-all is cheap.
   - Even 2,048 notes took 1.156 ms.
   - The expensive part is geometric marquee hit-testing, not maintaining the selected ID list.

## Audio Baseline

`Simple Osc Complex` now has a deterministic render test:

- two identical synth instances
- same patch
- same note-on/note-off sequence
- same render segment boundaries
- sample-for-sample matching output
- finite output
- bounded peak

Focused result:

```text
All tests passed (4127 assertions in 4 test cases)
```

## Verification Run

Completed:

- `cmake --build out/build/debug --target tsq_core_tests tsq_engine_integration_tests`
- `out/build/debug/tests/tsq_core_tests "[devices][simple-osc-complex]"`
- `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 out/build/debug/tests/tsq_engine_integration_tests "[integration][expression][baseline][perf]"`

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- JUCE assertion in `juce_Component.cpp:3022` when the headless probe paints a component tree that is not attached to a real desktop peer.
- Duplicate `src/core/libtsq_core.a` linker warning.

## Requirements For Future Expression Prompts

- Prompt 08 overlay rendering must compare paint results against this baseline.
- Prompt 09 release ghost selection must compare marquee selection results against this baseline.
- Prompt 14 generic expression playback must compare native synth sync/render costs against this baseline.
- Prompt 22 full performance pass must include all of these scenarios.

## Suggested Next Prompt

Run Prompt 01 - Core Expression Domain Types.

Reason:

- Baseline probes exist.
- Simple Osc deterministic audio behavior is covered.
- No Expression Mode model/UI has been added yet, so Prompt 01 can begin with clean architecture boundaries.
