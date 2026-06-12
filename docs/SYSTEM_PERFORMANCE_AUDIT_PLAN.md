# System Performance Audit Plan

Last updated: 2026-06-10

This plan breaks the whole DAW into context-window-sized audit segments. The app currently feels laggy, so each segment is designed to be investigated deeply in a fresh context without requiring the agent to reload the entire codebase.

Use this plan with `docs/AUDIO_THREAD_AUDIT.md`, `docs/DEBUGGING.md`, `docs/FEATURES.md`, and `docs/KNOWN_ISSUES.md` open as supporting context.

## Ground Rules

- Measure before optimizing. Every segment should produce at least one timing, allocation, repaint, or call-count observation before code changes.
- Keep each audit window scoped to one segment unless the evidence clearly crosses a boundary.
- Prefer small instrumentation hooks that can remain useful later, guarded by a debug flag or kept low-overhead.
- Do not run the expensive Synthesizer/VST parameter-wipe stress harness unless the segment explicitly needs it or the user asks.
- Preserve realtime boundaries from `docs/AUDIO_THREAD_AUDIT.md`: no logging, file IO, JSON/XML parsing, plugin scanning, UI calls, locks, allocation-heavy work, or large model traversal from realtime/audio-adjacent paths.
- Distinguish message-thread lag, engine-sync lag, audio glitches, and project-file IO lag. They can feel similar to a user but require different fixes.
- A segment is complete only when it ends with findings, changed files if any, verification commands, and remaining risk.

## Shared Baseline Metrics

Capture these once at the start, then re-run the relevant subset after each segment:

- App cold launch time to visible main window.
- Time from clicking/double-clicking a MIDI clip or note action to visible UI response.
- Timeline drag latency while moving/resizing clips.
- Piano-roll drag latency while moving notes vertically and horizontally.
- Time to create a MIDI note in a clip with the current test project.
- Time spent in `AppServices::syncPlaybackProjectIfNeeded()` and `TracktionPlaybackEngine::syncProject()`.
- Frequency and duration of UI timer callbacks in `MainComponent`, `TransportComponent`, `BrowserPanelComponent`, `TimelineComponent`, `PianoRollComponent`, and any meter/automation timers.
- Paint frequency and worst paint duration for Timeline, Piano Roll, Track List, Browser Panel, and Device Chain.
- Project save/load duration for a small project and a larger synthetic project.
- CPU usage while idle, while playback is stopped with the app visible, during playback, and during note editing.
- Diagnostics log write volume during a simple edit session.

Suggested tools:

- Xcode Instruments: Time Profiler, Allocations, File Activity, Main Thread Checker, Core Animation.
- macOS Activity Monitor for quick CPU and memory sanity checks.
- Existing diagnostics log at `~/Library/Application Support/TheorySequencer/diagnostics.log`.
- Focused debug timing logs or RAII scoped timers for message-thread methods.
- Catch2/integration tests for reproducible non-GUI paths.

## Standard Segment Handoff

Each audit segment should end with this exact shape:

```text
Performance Segment Handoff
- Segment:
- User-visible symptom investigated:
- Baseline measurements:
- Hot paths found:
- Changes made:
- Files changed:
- Verification run:
- Before/after result:
- Remaining risks:
- Suggested next segment:
```

If no code is changed, write `Changes made: None` and still record the evidence.

## Output Files For Audit Notes

The first agent who starts the audit should create:

- `docs/performance-audit/00_BASELINE.md`
- `docs/performance-audit/NN_<segment-name>.md` for each completed segment

Keep those notes brief and evidence-first. The plan file should remain the stable roadmap; per-segment notes should be the lab notebook.

## Segment 00 - Baseline And Instrumentation Harness

Goal:
- Establish a reproducible baseline before optimizing.
- Add or identify lightweight instrumentation that future segments can reuse.

Primary files:
- `src/app/AppServices.cpp`
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/ui/MainComponent.cpp`
- `src/ui/TimelineComponent.cpp`
- `src/ui/PianoRollComponent.cpp`
- `src/ui/TrackListComponent.cpp`
- `src/ui/BrowserPanelComponent.cpp`
- `src/core/diagnostics/Logger.*`
- `docs/DEBUGGING.md`
- `docs/AUDIO_THREAD_AUDIT.md`

Investigate:
- Is the lag present at idle, only after plugin assignment, after note creation, after playback, or after opening a large project?
- Does lag correlate with diagnostics log volume?
- Does lag correlate with playback-project dirtying or full Tracktion sync?
- Are timers repainting or refreshing when their views are hidden?

Measurements:
- Add scoped timing around app startup, project sync, note creation, clip drag commit, timer callbacks, and major paints.
- Count diagnostics log lines emitted during: launch, one note creation, one piano-roll click, return to zero, playback start.
- Record idle CPU for 60 seconds with no playback.

Deliverable:
- Baseline note in `docs/performance-audit/00_BASELINE.md`.
- A reusable timing/logging mechanism or a clear decision that Instruments is enough.

## Segment 01 - App Shell, Main Message Loop, And Global Timers

Goal:
- Find message-thread churn that makes the whole app feel sluggish even when no specific editor is active.

Primary files:
- `src/ui/MainComponent.cpp`
- `src/ui/TransportComponent.cpp`
- `src/ui/StatusBarComponent.cpp`
- `src/ui/DetailEditorComponent.cpp`
- `src/app/AppServices.cpp`

Investigate:
- Global timer frequency and work per tick.
- Whether hidden overlays or hidden lower-editor modes still refresh.
- Whether window title/status updates cause unnecessary layout/repaint.
- Whether global shortcuts/focus handling produce extra work on every key event.

Measurements:
- Timer callback counts and worst/average duration over 60 seconds idle.
- Repaint counts for main child components at idle.
- Main-thread call stacks from Instruments while the app sits idle.

Fix candidates:
- Stop timers when components are hidden.
- Replace polling with dirty flags/listeners.
- Coalesce status/title/UI refreshes.
- Reduce repaint regions.

Verification:
- Idle CPU before/after.
- No regression to Space, Shift+Tab, Escape, copy/paste/select-all.

## Segment 02 - Diagnostics, Logging, And VST Watchdog Verbosity

Goal:
- Determine whether debug tracing added for the VST reset bug is causing lag during editing.

Primary files:
- `src/app/AppServices.cpp`
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/core/diagnostics/Logger.*`
- `src/ui/PianoRollComponent.cpp`
- `docs/DEBUGGING.md`

Investigate:
- Plugin-state trace calls around piano-roll mouse down, note creation, dirty marking, sync, return-to-zero, and playback start.
- Whether log formatting or file/console output happens on every minor UI action.
- Whether `observeLivePluginParameterState()` or parameter snapshotting queries expensive plugin state too often.

Measurements:
- Log line count and bytes written for a 10-note editing session.
- Time spent in trace/snapshot methods during note creation and piano-roll clicks.
- CPU while editing with plugin-state diagnostics enabled.

Fix candidates:
- Add a runtime diagnostics verbosity switch.
- Rate-limit or sample plugin-state traces.
- Capture only changed watched parameters.
- Move heavy debug snapshots behind explicit user-triggered debug mode.

Verification:
- Reproduce one note creation and one return-to-zero flow.
- Confirm diagnostics still capture enough data for VST-state investigation when enabled.

## Segment 03 - Timeline Arrangement Rendering And Interaction

Goal:
- Reduce lag while dragging/resizing clips, moving the timeline playhead, zooming, and viewing many tracks/clips.

Primary files:
- `src/ui/TimelineComponent.cpp`
- `src/ui/TimelineComponent.h`
- `src/core/sequencing/Project.*`
- `src/core/time/*`
- `src/core/commands/ProjectMutationCommands.cpp`

Investigate:
- Paint cost for grid lines, bar/beat labels, harmonic lanes, clips, waveforms, automation lanes, playhead, and selection overlays.
- Whether timeline paint recomputes expensive geometry/theory every frame.
- Whether mouse drag emits command mutations continuously instead of previewing cheaply until commit.
- Whether contextual grid numbering scales poorly at high zoom or long timeline lengths.

Measurements:
- Timeline paint duration at 58 bars, 120 bars, and a synthetic many-track project.
- Mouse drag event count and per-event work while moving clips.
- Number of waveform thumbnail draws per paint.

Fix candidates:
- Cache grid geometry per viewport/zoom.
- Cache lane/clip rectangles until project or zoom changes.
- Draw only dirty regions during drag.
- Avoid project-wide scans from paint.
- Batch or defer expensive command commits until mouse up.

Verification:
- Clip drag/resize behavior, paste-to-playhead, marquee selection, contextual grid labels, timeline auto-extend.

## Segment 04 - Piano Roll Rendering, Lanes, And Note Editing

Goal:
- Reduce lag in the most note-dense editor: note entry, drag, marquee, scalar fill, harmonic headers, and lane generation.

Primary files:
- `src/ui/PianoRollComponent.cpp`
- `src/ui/PianoRollComponent.h`
- `src/core/sequencing/MidiClip.*`
- `src/core/sequencing/ClipHarmonicMap.*`
- `src/core/music_theory/*`
- `src/core/commands/ProjectMutationCommands.cpp`

Investigate:
- Lane generation across harmonic segments and accidental lanes.
- Paint cost for note labels, note blocks, headers, grid, overlays, playhead, selection, and drag previews.
- Whether note drag recalculates note names or harmonic context too broadly.
- Whether marquee selection scans all notes on every mouse move without spatial pruning.
- Whether scalar fill or scale-degree transpose writes more data than needed.

Measurements:
- Paint duration for clips with 10, 100, 500, and 2000 notes.
- Note drag event duration while updating labels in realtime.
- Marquee mouse-move duration for dense clips.
- Time from double-click to note visible.

Fix candidates:
- Cache visible lane model per clip/harmonic revision.
- Cache note label strings and invalidation keys.
- Spatially filter notes by visible viewport or marquee bounds.
- Separate drag preview state from committed project mutation.
- Limit plugin-state diagnostics on piano-roll mouse events through Segment 02 decisions.

Verification:
- Note creation, drag realtime note labels, arrow-key movement, copy/paste, scalar fill, harmonic headers, accidental lanes, Shift-marquee.

## Segment 05 - Browser Panel, Plugin Registry, And Project File Listing

Goal:
- Prevent browser/plugin/file discovery from making editing feel slow.

Primary files:
- `src/ui/BrowserPanelComponent.cpp`
- `src/ui/PluginBrowserComponent.cpp`
- `src/engine/plugins/PluginRegistry.*`
- `src/engine/plugins/PluginScanService.*`
- `src/app/AppServices.cpp`

Investigate:
- Whether project-file enumeration still happens on UI timers or broad refreshes.
- Search/filter allocation and sort cost.
- Plugin scan status timer behavior.
- Drag payload construction cost.
- Large `.tseq` package file listing behavior.

Measurements:
- Browser refresh duration with no package, small package, and large package.
- Plugin scan timer callback duration while a scan runs.
- Search/filter duration with a large registry cache.
- UI idle CPU with Browser Panel visible vs hidden.

Fix candidates:
- Background project-file indexer.
- Incremental filter model.
- Cache row view models.
- Throttle scan status UI updates.
- Avoid recursive file scans unless package path or explicit refresh changes.

Verification:
- Plugin scan/status, search/filter, project file rows, scale drag, audio/MIDI/plugin drag payloads.

## Segment 06 - Track Headers, Mixer UI, Meters, Routing, And Sends

Goal:
- Reduce lag from mixer rows and real-time-ish visual updates.

Primary files:
- `src/ui/TrackListComponent.cpp`
- `src/engine/PlaybackEngine.h`
- `src/engine/EngineTypes.h`
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/core/sequencing/Metering.*`
- `src/core/sequencing/MixerStrip.*`
- `src/core/sequencing/Routing.*`

Investigate:
- Meter polling cadence and whether every row repaints every tick.
- Cost of routing/send dropdown construction.
- Slider/text updates causing command execution too often.
- Layout/repaint work for many tracks and send controls.

Measurements:
- Track header paint duration with 1, 16, 64, and 128 tracks.
- Meter snapshot polling duration during playback and stop.
- Command count emitted by slider drags.
- Idle CPU with meters visible while stopped.

Fix candidates:
- Repaint only rows whose meter values changed materially.
- Apply meter ballistics in UI with cheap state.
- Commit slider commands at drag end or throttle intermediate commits.
- Cache routing menu choices until topology changes.

Verification:
- Volume dB input, pan, mute, solo, arm, meters, routing, sends, undo/redo.

## Segment 07 - Device Chain UI, Plugin Editors, And Drag/Drop

Goal:
- Ensure device-chain interactions do not trigger unnecessary engine rebuilds or UI churn.

Primary files:
- `src/ui/DeviceChainComponent.cpp`
- `src/app/AppServices.cpp`
- `src/core/commands/MixerCommands.*`
- `src/engine/TracktionPlaybackEngine.cpp`

Investigate:
- Drag-over repaint frequency and hit testing.
- Device insert/replace/reorder/remove command cost.
- Whether bypass uses fast live bypass or always full sync.
- Plugin editor lifetime and sync/editor close behavior.
- Plugin-state capture cost around device edits.

Measurements:
- Drag-over event duration in a chain with many devices.
- Time from device drop to UI response.
- Time from bypass click to audible/UI state change.
- Number of full `syncProject()` calls for append/replace/reorder/bypass.

Fix candidates:
- Fast-path bypass without full rebuild where safe.
- Debounce drag-over repaint.
- Cache faceplate geometry.
- Avoid duplicate legacy instrument/device-chain conversions.

Verification:
- Append, insert, replace, reorder, bypass, remove, editor open/close, undo/redo, save/reload state.

## Segment 08 - AppServices Commands, Dirtying, Undo/Redo, And Project Mutation

Goal:
- Identify project mutation paths that do too much synchronous work per user action.

Primary files:
- `src/app/AppServices.cpp`
- `src/app/AppServices.h`
- `src/core/commands/*`
- `src/core/sequencing/Project.*`
- `src/core/sequencing/Track.*`

Investigate:
- `markPlaybackProjectDirty()` call frequency.
- Whether simple edits trigger plugin-state restore scheduling or full engine sync too early.
- Undo/redo stack copying cost.
- Project-wide validation after small mutations.
- Expensive snapshots in command execution.

Measurements:
- Command execution duration for note add, note move, clip move, mixer edit, automation edit, device edit.
- Number of dirty marks per single user action.
- Time spent in project validation.

Fix candidates:
- Dirty categories: UI-only, MIDI-materialization, mixer-control, graph-topology, package-only.
- Defer sync until playback start or explicit preview need.
- Narrow validation to affected tracks/routes where possible.
- Use move-aware snapshots and avoid full project copies in hot paths.

Verification:
- Undo/redo, playback after edit, save/load, VST state preservation paths.

## Segment 09 - Tracktion Engine Sync And Playback Graph Materialization

Goal:
- Make `syncProject()` understandable, measurable, and eventually scalable.

Primary files:
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/engine/PlaybackEngine.h`
- `src/engine/EngineTypes.h`
- `src/app/AppServices.cpp`

Investigate:
- Full rebuild vs incremental update decisions.
- Plugin load/state restore cost.
- MIDI clip/note materialization cost.
- Audio clip materialization cost.
- Meter plugin creation/removal.
- Return/master/send routing setup.
- Automation snapshot preparation.

Measurements:
- Per-phase timings inside `syncProject()`.
- Sync time by project size: tracks, clips, notes, devices, automation points.
- Number of plugin instances recreated for each edit type.
- Time from pressing Play after edits to audio start.

Fix candidates:
- Phase-level sync telemetry.
- Incremental sync for non-topology changes.
- Prepared immutable playback snapshot.
- Background/cancellable project preparation for large projects.
- Avoid plugin reloads for MIDI note edits.

Verification:
- Playback start, meters, plugin state, note edits, clip edits, device changes, returns/master, automation.

## Segment 10 - Automation Editing And Playback Binding

Goal:
- Prevent automation lanes and the 30 Hz automation timer from causing lag.

Primary files:
- `src/ui/TimelineComponent.cpp`
- `src/core/sequencing/Automation.*`
- `src/core/sequencing/AutomationPlayback.*`
- `src/core/commands/MixerCommands.*`
- `src/engine/TracktionPlaybackEngine.cpp`

Investigate:
- Lane paint and hit testing with many points.
- Automation snapshot lookup cost during playback timer.
- Plugin parameter lookup/matching cost.
- Whether timer is inactive when no automation lanes exist.
- Whether moving points causes broad project sync.

Measurements:
- Automation lane paint duration with 10, 100, 1000 points.
- Playback timer duration with 0, 10, 100 lanes.
- Point drag event duration.
- Plugin parameter binding time on sync.

Fix candidates:
- Cache sorted point geometry.
- Use binary search for curve lookup where needed.
- Pre-bind automation targets during sync.
- Keep timer disabled unless automation exists.
- Dirty only automation binding/control state for lane edits that do not alter graph topology.

Verification:
- Volume/pan/send/plugin parameter automation playback, save/reload, device replacement invalidation.

## Segment 11 - Audio Files, Waveforms, Project Package IO, And Serialization

Goal:
- Keep file-heavy workflows from blocking editing.

Primary files:
- `src/ui/TimelineComponent.cpp`
- `src/core/sequencing/AudioClip.*`
- `src/core/serialization/ProjectSerializer.cpp`
- `src/core/serialization/ProjectPackage.cpp`
- `src/app/AppServices.cpp`

Investigate:
- Waveform thumbnail cache behavior.
- Audio file validation/import work on drop.
- Project package recursive enumeration.
- Save/load JSON and plugin-state file writes.
- Missing audio warnings.

Measurements:
- Import duration for small and large audio files.
- Time to first waveform draw.
- Save/load duration for projects with many clips/assets.
- File activity during idle and browser refresh.

Fix candidates:
- Background waveform indexing/progress.
- Cache package file listings.
- Async or staged save/load feedback for large packages.
- Avoid serializing unchanged heavy metadata repeatedly.

Verification:
- Audio import, waveform display, playback, missing-file warning, save/reload, Browser project-file rows.

## Segment 12 - Plugin Scanning, Plugin Registry, And Parameter Metadata

Goal:
- Ensure plugin discovery and parameter metadata do not degrade ordinary editing.

Primary files:
- `src/engine/plugins/PluginScanService.*`
- `src/engine/plugins/PluginRegistry.*`
- `src/ui/BrowserPanelComponent.cpp`
- `src/ui/PluginBrowserComponent.cpp`
- `src/app/AppServices.cpp`

Investigate:
- Scan worker behavior and registry writes.
- Parameter metadata discovery cost.
- UI updates during scan.
- Cache migration/load time.
- Failure/dead-man marker handling.

Measurements:
- Registry load time with realistic and artificially large caches.
- Scan status update frequency.
- Time spent normalizing/classifying plugins.
- App responsiveness during active scan.

Fix candidates:
- Batch registry writes.
- Separate scan progress from full cache refresh.
- Lazy-load heavy parameter metadata in UI.
- Lower scan status polling when app is busy.

Verification:
- Plugin scan, cache persistence, filter/search, effect/instrument classification, parameter automation menu population.

## Segment 13 - MIDI Input, Recording, Transport, And Playhead Polling

Goal:
- Keep live input and playhead updates responsive without flooding the message thread.

Primary files:
- `src/app/MidiInputRecordingService.*`
- `src/app/AppServices.cpp`
- `src/ui/TransportComponent.cpp`
- `src/ui/TimelineComponent.cpp`
- `src/ui/PianoRollComponent.cpp`
- `src/engine/TracktionPlaybackEngine.cpp`

Investigate:
- MIDI input event queue drain cadence.
- Quantization and Scale Lock processing cost.
- Recording clip extension and command frequency.
- Playhead polling and repaint cost in timeline/piano roll.
- Return-to-zero and playback start side effects.

Measurements:
- Message-thread cost to process N recorded MIDI events.
- Dropped MIDI event count under stress.
- Playhead polling duration and repaint regions during playback.
- Return-to-zero duration.

Fix candidates:
- Batch recording mutations.
- Surface dropped MIDI warnings without logging from callback.
- Repaint only playhead strips where possible.
- Coalesce playhead updates for hidden editors.

Verification:
- MIDI recording, quantize, Scale Lock, playback playhead, return-to-zero, no stuck notes.

## Segment 14 - Memory, Allocation, And Data Structure Scalability

Goal:
- Find broad allocation/copying problems that appear across features.

Primary files:
- `src/core/sequencing/*`
- `src/core/commands/*`
- `src/ui/*`
- `src/app/AppServices.cpp`
- `src/engine/TracktionPlaybackEngine.cpp`

Investigate:
- Full project copies in UI validation or undo/redo.
- Per-paint allocations.
- Per-mouse-move allocations.
- String formatting in hot paths.
- Large vectors repeatedly sorted/scanned.

Measurements:
- Instruments Allocations during 10-note editing, clip dragging, mixer slider drag, browser search, playback start.
- Allocation count per paint for Timeline and Piano Roll.
- Peak memory for synthetic large projects.

Fix candidates:
- Reuse scratch buffers.
- Cache computed labels/geometry.
- Replace repeated full scans with indexes keyed by track/clip/lane.
- Narrow command snapshots to affected entities.

Verification:
- Core tests plus targeted interactive smoke for edited paths.

## Segment 15 - Synthetic Project Fixtures And Regression Benchmarks

Goal:
- Create repeatable performance fixtures so lag fixes stay fixed.

Primary files:
- `tests/core/*`
- `tests/integration/*`
- `src/app/AppServices.cpp`
- `src/core/sequencing/*`
- Future benchmark helpers under `tests/performance` if added.

Investigate:
- What project sizes expose current lag without relying on private user projects.
- Which performance assertions are stable enough for CI and which should remain manual/local.

Measurements:
- Generate synthetic projects with:
  - 16, 64, 128 tracks.
  - 100, 1000, 5000 MIDI notes.
  - 10, 100, 1000 automation points.
  - Multiple audio clips with placeholder/wav fixtures.
  - Return tracks and device chains.
- Measure serializer, command, import, and sync durations where possible.

Fix candidates:
- Add non-flaky benchmark-style tests that print timings without failing CI by default.
- Add local-only performance commands or app debug menu items.
- Add a sample large `.tseq` fixture generator.

Verification:
- Benchmarks can be run from a clean checkout.
- Results are written to docs/performance-audit notes or diagnostics in a stable format.

## Segment 16 - Final Synthesis And Prioritized Fix Roadmap

Goal:
- Combine segment findings into a single prioritized lag-reduction roadmap.

Inputs:
- `docs/performance-audit/00_BASELINE.md`
- All `docs/performance-audit/NN_*.md` segment notes
- `docs/AUDIO_THREAD_AUDIT.md`
- `docs/KNOWN_ISSUES.md`

Deliverable:
- `docs/performance-audit/FINAL_PERFORMANCE_ROADMAP.md`

Roadmap should include:
- Top 5 user-visible lag sources with evidence.
- Quick wins that can be fixed safely.
- Architectural work that needs careful staging.
- Performance tests or instrumentation to keep.
- Performance tests or instrumentation to remove or gate.
- Risks to audio thread/realtime safety.
- Recommended order of implementation.

## Suggested Execution Order

1. Segment 00 - Baseline And Instrumentation Harness.
2. Segment 02 - Diagnostics, Logging, And VST Watchdog Verbosity.
3. Segment 01 - App Shell, Main Message Loop, And Global Timers.
4. Segment 04 - Piano Roll Rendering, Lanes, And Note Editing.
5. Segment 03 - Timeline Arrangement Rendering And Interaction.
6. Segment 08 - AppServices Commands, Dirtying, Undo/Redo, And Project Mutation.
7. Segment 09 - Tracktion Engine Sync And Playback Graph Materialization.
8. Segment 06 - Track Headers, Mixer UI, Meters, Routing, And Sends.
9. Segment 10 - Automation Editing And Playback Binding.
10. Segment 05 - Browser Panel, Plugin Registry, And Project File Listing.
11. Segment 11 - Audio Files, Waveforms, Project Package IO, And Serialization.
12. Segment 07 - Device Chain UI, Plugin Editors, And Drag/Drop.
13. Segment 12 - Plugin Scanning, Plugin Registry, And Parameter Metadata.
14. Segment 13 - MIDI Input, Recording, Transport, And Playhead Polling.
15. Segment 14 - Memory, Allocation, And Data Structure Scalability.
16. Segment 15 - Synthetic Project Fixtures And Regression Benchmarks.
17. Segment 16 - Final Synthesis And Prioritized Fix Roadmap.

The first three segments are intentionally front-loaded because they can explain lag everywhere else: verbose diagnostics, global timers, and message-thread churn can make every subsystem feel guilty even when only one shared path is actually hot.
