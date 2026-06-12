# Segment 03 - Timeline Arrangement Rendering And Interaction

Date: 2026-06-10

## Scope

Measured and reduced Timeline paint cost while the app is stopped/idle.

This segment focused on `TimelineComponent::paint()` because Segment 01 reduced idle repaint frequency but still showed Timeline as the largest recurring UI paint when it did run. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

The Timeline can make the whole DAW feel laggy because it is a central, frequently repainted view. Even after reducing idle repaint frequency, each Timeline repaint was still expensive enough to matter.

## Baseline Measurements

Segment 01 after-change baseline:

| Label | Samples | Avg | Max |
|---|---:|---:|---:|
| `TimelineComponent::paint` | 4 | 8.469 ms | 14.193 ms |
| `TrackListComponent::paint` | 4 | 1.701 ms | 4.605 ms |
| `MainComponent::timerCallback` | 33 | 0.113 ms | 2.083 ms |

Fresh pre-change spot check for this segment showed post-startup Timeline paints around 8.8-9.2 ms.

Initial Timeline phase trace after adding scoped sub-timers showed:

| Phase | Typical Post-Startup Range |
|---|---:|
| `TimelineComponent::paint grid-build` | 0.4-0.5 ms |
| `TimelineComponent::paint grid-draw` | 3.4-4.6 ms |
| `TimelineComponent::paint chord-regions` | 0.0-0.004 ms |
| `TimelineComponent::paint` | 7.3-8.9 ms |

Interpretation:
- Grid construction was not the main cost.
- Drawing grid lines was a major cost because the paint path drew long full-height lines before most were covered by structure-lane and track-row backgrounds.
- Chord borrowed-color analysis was negligible once scale-library construction was skipped for empty chord-region sets.

## Changes Made

Optimized Timeline paint in three focused ways:

- Grid-line de-duplication now uses append, sort, and merge instead of searching the existing vector for every line.
- Borrowed-chord scale-library construction is now lazy and only happens when there are chord regions to draw.
- Global grid drawing is clipped to useful Timeline areas instead of drawing full-height lines that are immediately covered.
- Row and automation grid lines use integer `fillRect()` segments instead of float vertical line strokes.
- Added opt-in phase timers for:
  - `TimelineComponent::paint grid-build`
  - `TimelineComponent::paint grid-draw`
  - `TimelineComponent::paint chord-regions`
  - `TimelineComponent::paint structure`
  - `TimelineComponent::paint tracks`

Files changed:
- `src/ui/TimelineComponent.cpp`
- `docs/performance-audit/03_TIMELINE_RENDERING_AND_INTERACTION.md`

## Before/After Result

Final post-change trace used:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer
```

The app was sampled for about 10 seconds and then stopped.

Post-change steady-state Timeline samples after startup:

| Label | Typical Range | Notes |
|---|---:|---|
| `TimelineComponent::paint` | 3.0-4.4 ms | One later sample reached 4.1 ms; startup paint excluded. |
| `TimelineComponent::paint grid-build` | 0.3-0.4 ms | Stable. |
| `TimelineComponent::paint grid-draw` | 0.3-0.4 ms | Down from about 3.4-4.6 ms. |
| `TimelineComponent::paint structure` | 1.3-2.4 ms | Now the largest named Timeline phase. |
| `TimelineComponent::paint tracks` | 0.55-0.69 ms | Low in the sampled project. |
| `TimelineComponent::paint chord-regions` | 0-0.007 ms | Cheap when no chord regions are visible. |

Delta:

| Measurement | Before | After | Change |
|---|---:|---:|---:|
| Steady Timeline paint | about 8.8-9.2 ms | about 3.0-4.4 ms | roughly 50-65% faster |
| `grid-draw` phase | about 3.4-4.6 ms | about 0.3-0.4 ms | roughly 85-90% faster |
| `chord-regions` with no chords | scale library built every paint | 0-0.007 ms | unnecessary work removed |

## Hot Paths Found

1. Full-height grid drawing was the biggest avoidable Timeline paint cost.
   - Most of those pixels were overpainted by structure lanes and track rows.
   - Clipping global grid drawing made the cost drop sharply.

2. Structure lanes are now the largest named Timeline paint phase.
   - They draw several rounded lane backgrounds and harmonic labels every paint.
   - A later pass could cache static structure-lane backgrounds or reduce rounded-rectangle work.

3. Timeline paint still does project-fit bookkeeping at paint time.
   - `ensureTimelineFitsAllClips()` remains in `paint()`.
   - It was not the measured bottleneck in this small project, but avoiding project-wide scans from paint remains a good follow-up for larger projects.

4. Track rendering was not a major cost in the sampled project.
   - A synthetic many-track/many-clip project is still needed before concluding the track loop is healthy at scale.

## Verification Run

- `cmake --build --preset debug --target tsq_app`
- Manual launch with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
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

Known existing warnings/notes:
- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.

## Remaining Risks

- The final measurements are from an idle/default project, not a synthetic 120-bar or many-track project.
- Drag/resize event latency still needs a direct interaction trace.
- Structure-lane rendering is now the visible Timeline paint hot spot.
- `ensureTimelineFitsAllClips()` still runs from paint and should be revisited in a larger-project pass.
- The track rendering phase should be re-measured with audio clips, waveforms, visible automation lanes, and many MIDI clips.

## Suggested Next Segment

Run Segment 04 - Piano Roll Rendering, Lanes, And Note Editing.

Reason:
- The Piano Roll is likely the next most user-sensitive editor after Timeline.
- It has dense note rendering, harmonic headers, accidental lanes, marquee selection, scalar fill, note labels, and drag previews, all of which can feel laggy during normal composition.
