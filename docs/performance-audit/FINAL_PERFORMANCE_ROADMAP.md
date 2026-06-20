# Final Performance Roadmap

Date: 2026-06-14

## Scope

Synthesizes Segments 00 through 15 of the system-wide performance audit into a prioritized roadmap.

This pass did not run the expensive Synthesizer/VST parameter-wipe stress harness. The goal is to preserve the measured wins, identify the remaining user-visible lag sources, and define context-window-sized next segments.

## Current State

The audit removed several broad interaction stutters:

| Area | Before | After | Source |
|---|---:|---:|---|
| Idle main timer average | 0.758 ms | 0.113 ms | Segment 01 |
| Idle Timeline paint count in short run | 34 samples | 4 samples | Segment 01 |
| Steady Timeline paint | about 8.8-9.2 ms | about 3.0-4.4 ms | Segment 03 |
| Piano-roll warm paint, 2,000 notes | 147.042 ms | 34.105 ms | Segment 04 |
| Browser repeated same-package refresh | 84.251 ms baseline path | 1.450 ms cache hit | Segment 05 |
| Track List warm refresh, 128 tracks | 5,289.382 ms | 1.870 ms | Segment 06 |
| Device-chain drag sweep, 96 devices | 294.065 ms | 20.728 ms | Segment 07 |
| AddNote, 2,048-note clip | 3.681 ms | 1.326 ms | Segment 08 |
| In-place sync, mixer-only 8,192-note project | 459.872 ms | 2.302 ms | Segment 09 |
| Automation curve construction, 5,000 points | 1,955.433 ms | 3.106 ms | Segment 10 |
| Dense audio timeline warm paint | 220.062 ms | 52.491 ms | Segment 11 |
| Plugin stable-ID lookup, 2,860 lookups | 285.694 ms | 16.899 ms | Segment 12 |
| Piano-roll playhead movement, 4,096 updates | 3,857.124 ms | 11.263 ms | Segment 13 |
| AppServices ten-note AddNote allocations | 1,495 / 590,160 bytes | 24 / 2,320 bytes | Segment 14 |

The remaining lag is no longer one single obvious loop. It is a set of heavier bulk operations, synchronous engine boundaries, and dense paint paths.

## Top Remaining Lag Sources

1. Bulk MIDI import and large project serialization.

   Evidence:
   - Segment 15 measured 5,000-note MIDI import at 1,670.679 ms.
   - Segment 15 measured large synthetic JSON serialize/deserialization at 547.676 ms / 337.469 ms.
   - Segment 11 still lists synchronous whole-project package save as a remaining risk.

   User-visible symptom:
   - Importing MIDI, loading/saving large songs, and package operations can freeze the app long enough to feel broken.

   Likely direction:
   - Build imported notes per clip in bulk, reserve once, sort once, and avoid command-style per-note mutation.
   - Add bulk mutation scopes for import/load/paste paths so they emit one dirty event and one sync request.
   - Keep serialization off the UI thread where possible, or at least add progress/cancel affordances before larger async work.

2. Tracktion sync and playback-start materialization.

   Evidence:
   - Segment 15 measured Tracktion sync smoke at 115.697 ms for 16 tracks / 1,000 MIDI notes.
   - Segment 14 measured playback start at 29,102 allocations / 1,505,256 bytes.
   - Segment 09 improved compatible in-place sync sharply, but one-note-change sync still measured 59.565 ms and full sync remains synchronous.

   User-visible symptom:
   - Pressing Play after edits, returning to zero, or changing project structure can stall while the playback graph catches up.

   Likely direction:
   - Introduce explicit dirty categories: note data, clip timing, automation, mixer, device structure, plugin parameter state, harmonic metadata.
   - Extend sync reuse below the track level so one changed clip does not force unnecessary track-wide clip materialization.
   - Stage full graph preparation away from the most immediate UI action when behavior allows it.

3. Dense UI paint and paint-time allocations.

   Evidence:
   - Segment 04 reduced 2,000-note piano-roll paint to 34.105 ms, but note labels still dominate dense warm paints.
   - Segment 14 still measured piano-roll warm paint at 10,384 allocations / 1,324,637 bytes.
   - Segment 14 measured timeline warm paint at 25,892 allocations / 1,168,735 bytes.
   - Segment 10 still lists uncached timeline automation curve geometry as a risk.
   - Segment 11 still measured dense audio timeline warm paint at 52.491 ms after optimization.

   User-visible symptom:
   - Big MIDI clips, visible automation, and dense audio arrangements can drop frames during scroll, zoom, playback, or selection changes.

   Likely direction:
   - Add level-of-detail behavior for note labels, automation point handles, and tiny audio clip detail.
   - Cache static layer geometry/images by viewport, zoom, and project revision.
   - Add dirty-region paint probes so playhead, selection, and hover updates are verified by actual repaint area.

4. Plugin registry, metadata, and browser/package first-hit work.

   Evidence:
   - Segment 12 improved lookup/filtering, but parameter-heavy XML save/load still measured about 3.2 seconds.
   - Segment 05 reduced repeated package refreshes to cache hits, but first package scan remains synchronous.
   - Segment 12 still lists first snapshot copies from `PluginRegistry::plugins()` and deferred no-copy lookup APIs.

   User-visible symptom:
   - Opening plugin-heavy browser views, refreshing package projects, or loading large plugin metadata caches can pause the UI.

   Likely direction:
   - Add no-copy plugin registry iteration APIs for browser and automation menus.
   - Move plugin metadata cache load/save to a background task with atomic swap on completion.
   - Keep manual reload for package file scans, but make the first scan progressive or cancelable.

5. Startup, diagnostics, and unmeasured real-plugin paths.

   Evidence:
   - Segments 00 and 01 measured `AppServices startup body` around 3.1-3.3 seconds.
   - Segment 02 showed always-on plugin-state logging made dirty marking about 35x slower in a no-plugin proxy path before it was gated.
   - `docs/KNOWN_ISSUES.md` still records a JUCE plugin-format assertion during launch.
   - Real plugin editor open/load timings were not measured in Segments 07, 09, 10, or 12.

   User-visible symptom:
   - Cold launch feels slow, and real VST workflows may still hide expensive startup/editor/snapshot work not covered by synthetic tests.

   Likely direction:
   - Split startup timing into plugin registry load, Tracktion initialization, audio device restore, package/default-project setup, and first UI show.
   - Keep plugin-state traces opt-in.
   - Add targeted real-plugin timing only when investigating plugin-specific behavior.

## Fixed Wins To Preserve

- Keep `TSQ_PERF_TRACE` and `TSQ_PLUGIN_STATE_TRACE` off by default.
- Preserve state-aware main timer behavior. Avoid reintroducing unconditional idle repaint/refresh loops.
- Preserve Track List row/model fingerprints and routing topology caching.
- Preserve piano-roll visible-lane and visible-note culling.
- Preserve Tracktion compatible in-place sync clip reuse.
- Preserve ordered automation insertion and no-copy automation paint access.
- Preserve MIDI clip edit headroom and bulk reserve behavior after load/import.

## Recommended Next Segments

### Segment 17 - MIDI Import And Bulk Mutation Paths

Why first:
- It is the largest single measured remaining hot path: 5,000-note MIDI import took 1.671 seconds.
- The same bulk APIs can help paste, scalar fill, project load, and future recording compaction.

Work items:
- Add a focused `[integration][midi-import][perf]` probe with the Segment 15 large fixture.
- Trace MIDI import by parse, note construction, clip insertion, sorting, harmonic interpretation, dirtying, and serialization update.
- Implement per-clip bulk insertion that reserves once and sorts once.
- Add a mutation batching API for import/load paths so undo, dirtying, UI refresh, and playback sync are coalesced.
- Re-run Segment 15 synthetic benchmark and compare MIDI import, serialize, and AddNote.

Definition of done:
- Large 5,000-note import is reduced by an order of magnitude or the remaining bottleneck is isolated to a specific external/parser phase.
- Existing import behavior and note spelling remain correct.
- Core tests, package/import integration, and synthetic benchmark pass.

### Segment 18 - Sync Dirty Categories And Playback Start

Why second:
- Playback-start allocations and Tracktion materialization remain large enough to explain stalls after edits.

Work items:
- Add explicit dirty categories to AppServices and command execution.
- Make automation-only, mixer-only, harmonic-metadata-only, and note-only edits request the narrowest safe sync path.
- Extend in-place sync reuse from track-level to clip-level where Tracktion constraints allow it.
- Profile playback start after return-to-zero and after a single MIDI edit.

Definition of done:
- One-note-change sync is materially below the current 59.565 ms result.
- Playback-start allocation count is reduced or attributed to Tracktion internals with a clear mitigation plan.
- Plugin state preservation remains covered by existing regression tests, without running the expensive manual stress harness.

### Segment 19 - Dense Paint, Dirty Regions, And UI Allocation Pass

Why third:
- After timer and refresh fixes, the remaining visible stutter is per-frame paint cost in dense editors.

Work items:
- Add paint-region probes for Timeline and Piano Roll.
- Add level-of-detail thresholds for dense note labels, automation point handles, and tiny audio clip decoration.
- Cache timeline automation curve geometry and structure-lane backgrounds.
- Reduce timeline/piano-roll warm paint allocations found in Segment 14.

Definition of done:
- Dense piano-roll and timeline paints stay under a frame-budget target in debug probes, or the slowest remaining phase is explicitly documented.
- Playhead, hover, and selection updates repaint narrow regions instead of broad surfaces.

### Segment 20 - Project, Package, Waveform, And Plugin Metadata IO

Why fourth:
- IO pauses are less frequent than note entry/playback, but they are severe when they happen.

Work items:
- Split project save/load/package operations into measured phases.
- Add async or progressive package save/load where feasible.
- Move plugin metadata XML load/save to a background path.
- Add cache invalidation rules for waveform thumbnails and package source existence checks.

Definition of done:
- Large-project save/load no longer blocks the UI without feedback.
- Plugin metadata cache persistence no longer creates multi-second foreground stalls.

### Segment 21 - Startup And Launch Time

Why fifth:
- Startup is consistently around 3 seconds, but it is less disruptive than in-session editing lag.

Work items:
- Split `AppServices startup body` into startup phase timers.
- Delay nonessential plugin/browser/package work until after first UI show.
- Investigate the JUCE plugin-format assertion separately from performance tuning.

Definition of done:
- Startup has a phase-by-phase breakdown and at least one expensive nonessential phase is deferred or optimized.

### Segment 22 - Live MIDI Recording And Real-Plugin Timing

Why last in this roadmap:
- The audio/MIDI callback paths appear intentionally lightweight, but live hardware and real plugins still need targeted tests.

Work items:
- Add deterministic MIDI event injection for recording stress.
- Surface dropped MIDI FIFO event counts in debug UI or diagnostics.
- Add real-plugin editor open/snapshot timing only for focused plugin investigations.

Definition of done:
- MIDI recording can be stress-tested without physical hardware.
- Real-plugin timing can be measured without enabling broad verbose logging.

## Regression And Instrumentation Strategy

- Keep focused probes by subsystem, and use Segment 15 synthetic fixtures as the broad smoke benchmark.
- Add timing budgets only after each hot path has been optimized once; avoid locking in today's slow baselines as acceptable.
- Prefer probes that print a small fixed table over full trace dumps.
- Keep `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0` for investigations only. It is intentionally noisy and can perturb tiny timings.
- Add CI-friendly tags for targeted performance checks:
  - `[integration][synthetic][perf]`
  - `[integration][midi-import][perf]`
  - `[integration][sync][perf]`
  - `[integration][ui-paint][perf]`
  - `[integration][memory][perf]`

## Audio And Realtime Guardrails

- Do not call `Logger::log`, perform file IO, allocate large objects, or take broad locks from audio or MIDI callbacks.
- Keep MIDI input callback behavior fixed-size and allocation-free.
- Treat Tracktion audio render as the realtime boundary. App-side sync, serialization, plugin scanning, and waveform work must stay outside it.
- Any background sync or IO work must hand off immutable snapshots or clearly owned copies, then publish results on the message thread.
- Plugin parameter observation/restoration should remain control-rate and opt-in traceable, not audio-thread logging.

## Segment 16 Handoff

- Segment: 16 - Final Performance Roadmap
- User-visible symptom investigated: system-wide lag after the focused audit segments.
- Baseline measurements: drawn from Segments 00 through 15, especially Segment 15 synthetic fixtures.
- Hot paths found: MIDI import, serialization/package IO, Tracktion sync/playback start, dense UI paint allocations, plugin metadata persistence, startup.
- Changes made: roadmap only; no production code changes.
- Files changed: this report.
- Suggested next segment: 17 - MIDI Import And Bulk Mutation Paths.
