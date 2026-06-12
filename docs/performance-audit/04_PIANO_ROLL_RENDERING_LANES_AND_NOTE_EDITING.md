# Segment 04 - Piano Roll Rendering, Lanes, And Note Editing

Date: 2026-06-10

## Scope

Measured and reduced Piano Roll paint cost for dense MIDI clips with harmonic headers and scale-aware note lanes.

This segment focused on `PianoRollComponent.cpp`, especially `RollContentComponent::paint()`, because note entry, marquee selection, drag previews, harmonic lane headers, and dense note drawing all depend on this view feeling immediate. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

The Piano Roll can feel laggy while editing because it repaints a large theory-aware grid: harmonic lane headers, contextual scale/accidental rows, chord overlays, bar/beat grid lines, note labels, selected notes, marquee state, and the paste playhead.

## Baseline Measurements

Added an integration performance probe that renders a synthetic 64-beat MIDI clip with harmonic changes and note counts of 10, 100, 500, and 2000.

Baseline before this segment's optimization:

| Note Count | Render | `paint` | `paint lanes` | `paint notes` |
|---:|---:|---:|---:|---:|
| 10 | 79.190 ms | 59.278 ms | 45.857 ms | 0.335 ms |
| 100 | 76.549 ms | 64.938 ms | 45.836 ms | 3.161 ms |
| 500 | 86.406 ms | 75.330 ms | 48.401 ms | 10.973 ms |
| 2000 | 163.106 ms | 147.042 ms | 56.306 ms | 70.740 ms |

Initial hot paths:

- Lane/background drawing was the largest cost at low note counts.
- Dense note rendering became dominant at 2000 notes.
- The paint path computed note screen bounds before rejecting many offscreen notes.
- Grid and harmonic overlay work was smaller than lanes/notes but still worth keeping visible in phase traces.

## Changes Made

- Added opt-in phase timers for:
  - `PianoRollContent::paint model`
  - `PianoRollContent::paint lanes`
  - `PianoRollContent::paint lanes-cache-build`
  - `PianoRollContent::paint lanes-cache-draw`
  - `PianoRollContent::paint grid`
  - `PianoRollContent::paint harmonic-overlay`
  - `PianoRollContent::paint notes`
  - `PianoRollContent::paint adornments`
  - `PianoRollContent::visiblePitchSegments`
  - `PianoRollContent::visiblePitchLanesForSegment`
  - `PianoRollContent::selectNotesInMarquee`
- Added `tests/integration/PianoRollPerformanceProbeTest.cpp` with the tag `[integration][piano-roll][perf]`.
- Made the paint path derive a visible clip rectangle from the graphics clip and parent viewport.
- Added visible X/Y culling for note paint:
  - Notes outside the visible local tick ranges are skipped before bounds generation.
  - Offscreen lane rows are skipped before note rectangles are created.
  - Drag preview paint remains conservative to avoid hiding moved notes.
- Added a viewport-slice cache for the static lane background:
  - The cache stores only the current visible rectangle, not the full clip surface.
  - The cache key includes lane layout, harmonic segment context, row height, zoom, chromatic reveal state, and lane spelling/status.
  - Cache builds skip offscreen harmonic segments and offscreen lane rows.
- Replaced some grid-line drawing with clipped integer `fillRect()` calls in this path.

Files changed:

- `src/ui/PianoRollComponent.cpp`
- `tests/integration/PianoRollPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/04_PIANO_ROLL_RENDERING_LANES_AND_NOTE_EDITING.md`

## Before/After Result

Final post-change probe command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][piano-roll][perf]"
```

Final internal paint timings:

| Note Count | Cold `paint` | Warm `paint` | Warm `paint lanes` | Warm `paint notes` |
|---:|---:|---:|---:|---:|
| 10 | 58.092 ms | 23.490 ms | 1.129 ms | 0.367 ms |
| 100 | 68.501 ms | 14.962 ms | 1.427 ms | 3.061 ms |
| 500 | 59.446 ms | 20.399 ms | 2.131 ms | 7.180 ms |
| 2000 | 84.159 ms | 34.105 ms | 1.311 ms | 21.825 ms |

Delta against the original baseline:

| Note Count | Baseline `paint` | Warm `paint` After | Change |
|---:|---:|---:|---:|
| 10 | 59.278 ms | 23.490 ms | about 60% faster |
| 100 | 64.938 ms | 14.962 ms | about 77% faster |
| 500 | 75.330 ms | 20.399 ms | about 73% faster |
| 2000 | 147.042 ms | 34.105 ms | about 77% faster |

Notes:

- Cold paint is still dominated by lane cache construction, usually around 40-52 ms in the probe.
- Warm lane redraw is now usually around 1-2 ms.
- Dense note paint still dominates warm 2000-note paints at about 22 ms.
- The outer probe render timer had occasional outliers even when internal `paint` timers were stable, so the segment conclusions use internal paint phase timings.

## Hot Paths Found

1. Lane/background drawing was expensive enough to cache.
   - Caching the full content surface made first paint too expensive.
   - Caching only the visible slice kept warm redraws fast without a huge cold-memory surface.

2. Dense note rendering still scales with visible notes.
   - Time-window culling removed much of the offscreen work.
   - A future pass should consider caching note labels or building a spatial note index for dense clips.

3. Lane generation is repeated frequently.
   - `visiblePitchSegments()` and `visiblePitchLanesForSegment()` are now timed.
   - They are not the largest measured paint phase here, but they are still called during resize/open/paint and may be worth model-level caching later.

4. The integration probe still uses offscreen component painting.
   - It passes and produces useful timings.
   - It emits a known JUCE component assertion from the offscreen render path.

## Verification Run

- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][piano-roll][perf]"` with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app` and verified matching binary hashes.

Result:

- Debug app target rebuilt successfully.
- Core test target built and `ctest --preset tests` passed.
- Focused integration target rebuilt successfully.
- Piano Roll performance probe passed.
- Focused non-VST meter integration passed when run by itself. A first attempt run in parallel with the package/import integration failed to observe meter activity, so avoid parallelizing audio-engine integrations.
- Focused non-VST package/import integration passed.
- Both debug app bundle paths contain the same binary hash: `2758e8e2b9dbcb6327125ffd169d8b7e33814efe097627f14a495ebbcb8a6fd2`.

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- JUCE assertion in `juce_Component.cpp:3022` from the offscreen performance probe render path.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- Locale warnings from `shasum` because the shell environment uses `C.UTF-8`, which this macOS Perl build does not support.

## Remaining Risks

- This segment measured paint, not direct mouse-drag event latency.
- The probe does not yet simulate marquee mouse-move selection over a dense clip.
- The lane cache invalidates when the visible viewport slice changes; scrolling still requires rebuilding the newly visible slice.
- Dense note label rendering is still the likely next Piano Roll paint target.
- The offscreen probe assertion should be cleaned up by rendering through a visible test window or another JUCE-approved render path.

## Suggested Next Segment

Run Segment 05 - Browser Panel, Plugin Registry, And Project File Listing.

Reason:
- The Piano Roll now has a reusable performance probe and significantly faster warm paints.
- Browser and plugin registry work can create whole-app lag that feels unrelated to the current editor, especially when the Browser Panel is visible or plugin scans/status updates are active.

## Performance Segment Handoff

- Segment: 04 - Piano Roll Rendering, Lanes, And Note Editing
- User-visible symptom investigated: lag during note-dense piano-roll editing and repaint-heavy workflows.
- Baseline measurements: original 2000-note paint was 147.042 ms, with 70.740 ms in notes and 56.306 ms in lanes.
- Hot paths found: static lane/background drawing, dense note bounds/label rendering, repeated lane model generation, offscreen render-probe assertion.
- Changes made: phase timers, integration performance probe, visible paint bounds, visible tick culling, vertical note culling, viewport-slice lane cache.
- Files changed: `src/ui/PianoRollComponent.cpp`, `tests/integration/PianoRollPerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, `docs/performance-audit/04_PIANO_ROLL_RENDERING_LANES_AND_NOTE_EDITING.md`.
- Verification run: debug app build, core tests, focused performance probe, meter/package integrations, and debug bundle sync/hash check.
- Before/after result: warm 2000-note paint dropped from 147.042 ms to 34.105 ms; warm lane paint dropped to about 1-2 ms.
- Remaining risks: drag/marquee event latency not directly simulated, note labels still dominate dense warm paints, offscreen probe assertion remains.
- Suggested next segment: 05 - Browser Panel, Plugin Registry, And Project File Listing.
