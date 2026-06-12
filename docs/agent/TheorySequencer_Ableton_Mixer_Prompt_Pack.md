# TheorySequencer Ableton-Style Mixer Upgrade — Context-Window-Friendly Prompt Pack

Source inputs used to create this pack:

- `FEATURES.md`: current shipped app architecture and feature inventory.
- `Ableton_Style_Mixer_Features.txt`: requested Ableton-style mixer upgrade requirements.

Use these prompts sequentially with your coding agent. Each prompt is designed to fit in a modest context window and to produce a concrete, reviewable slice of the implementation. Do not ask the agent to implement the entire mixer in one context window. Give it the current repository, the previous prompt's handoff summary, and the next prompt only.

## Non-Negotiable Engineering Contract For Every Prompt

Paste this contract above every implementation prompt unless the agent already has it in its active context.

```text
You are implementing TheorySequencer, an industry-leading DAW written in C++20 with JUCE and Tracktion Engine. Audio quality is paramount. Performance is the second-highest priority. Correctness, maintainability, and musical workflow quality are mandatory.

Non-negotiable rules:
1. Cut no corners. Do not ship placeholder UI, fake DSP, stubbed persistence, simulated plugin behavior, or optimistic TODOs as completion.
2. Preserve existing behavior unless the prompt explicitly changes it. The current app already has MIDI tracks, clips, piano roll, scale-aware lanes, theory tools, project packages, VST3 instrument hosting, Tracktion-backed playback, diagnostics, command-backed undo/redo, and tests.
3. Keep the existing architecture boundaries: core model and UI must not depend directly on Tracktion internals. Tracktion remains behind the app-owned playback/engine abstraction.
4. All user-visible project mutations must be command-backed, undoable where musically expected, serializable, and covered by tests.
5. The audio thread must never perform plugin scanning, filesystem work, logging/string formatting, blocking locks, UI calls, heap-heavy work, or unbounded allocation. Use atomics, lock-free queues, preallocation, immutable snapshots, and message/control-thread synchronization.
6. Prefer typed IDs, explicit value types, bounded invariants, non-throwing result paths, and deterministic migrations over stringly-typed state.
7. Parameter changes affecting audio must be click-free: smooth gain/pan/automation changes, avoid zipper noise, and preserve sample accuracy or block-accurate behavior where Tracktion permits.
8. Update diagnostics and user-facing errors. Fail loudly and helpfully; never silently ignore missing plugins, invalid routing, broken audio files, or automation target mismatches.
9. Write or update tests before declaring completion. Run the relevant test targets and report exact commands and results.
10. End each task with a concise handoff summary: files changed, architecture decisions, tests run, known risks, and the next recommended prompt.
```

---

## Prompt 00 — Repository Audit And Implementation Map

```text
Task: Audit the current repository and produce a precise implementation map for the Ableton-style mixer upgrade. Do not make product-code changes except optional documentation notes.

Context:
- Current app: C++20, JUCE, Tracktion Engine, modular targets (`tsq_core`, `tsq_engine`, `tsq_ui`, `tsq_app`, tests), AppServices container, playback engine abstraction, command stack, serialization, VST3 instrument hosting, piano roll, scale palette, timeline, diagnostics, MIDI recording/input, and project package persistence.
- Upgrade target: right-docked tabbed Browser Panel; horizontal mixer controls in track headers; drag-to-create tracks; context menus; lower Device Chain view; VST3 audio-effect hosting; automation lanes; return tracks; master track; audio tracks with waveform rendering.

Instructions:
1. Inspect the repository structure, CMake targets, core model classes, playback engine wrapper, plugin scanning/cache code, UI components, serialization code, command framework, and test suite.
2. Identify exactly where each upgrade feature should live: core model, command layer, engine adapter, UI, app services, persistence, tests.
3. Identify existing code that must be preserved, especially scale palette behavior, plugin browser behavior, piano roll behavior, command-backed undo/redo, and playback sync boundaries.
4. Identify audio-thread risks and current realtime boundaries.
5. Produce a staged implementation plan that can be executed by the later prompts in this pack.

Acceptance criteria:
- Produce a source map with file/class names and responsibilities.
- Produce a feature-to-code mapping for all upgrade features.
- Produce a dependency ordering that prevents UI work from racing ahead of core/engine support.
- Produce a risk register for audio quality, performance, persistence migration, plugin state, routing cycles, and automation timing.
- Do not claim implementation is complete. This prompt is audit-only.
```

---

## Prompt 01 — Core Mixer, Track, Routing, Device, And Automation Domain Model

```text
Task: Extend the core model to support a complete mixer architecture without touching Tracktion-specific engine internals or building UI yet.

Implement elegant core value types and invariants for:
1. Track types: MIDI, Audio, Return, and Master. Preserve existing unlimited MIDI tracks and migrate existing tracks cleanly.
2. Mixer strip state per applicable track: volume in dB and linear gain, pan, mute/activator, solo, meter identity/source, track color/name where applicable, and routing selections.
3. Device chains: stable device-slot IDs, plugin references, plugin kind metadata, bypass state, order, state-file association, and clear chain rules.
4. Routing: typed Audio From, Audio To, MIDI From, MIDI To, return-send targets, master output, and sidechain-capable references. Add validation and cycle prevention.
5. Automation targets: Volume, Pan, Mute if appropriate, Send Level, Device Bypass if appropriate, and VST3 parameter targets identified by stable plugin/device/parameter IDs.
6. Automation curves: vector-editable points with musical-time positions, normalized values, interpolation type, validation, and efficient query primitives.
7. Audio track clip placeholders needed for later waveform/audio playback work: audio source reference, clip start/length, source offset, looping/stretch flags if already supported by Tracktion abstractions; keep design non-destructive.

Instructions:
- Preserve existing MIDI clip, harmonic metadata, piano-roll, theory, tempo/time-signature, and custom-scale behavior.
- Use strong types and stable IDs. Avoid raw strings for routing/device references except for user-facing labels and persisted plugin identifiers.
- Add command types for creating MIDI/Audio/Return tracks, changing mixer state, changing routing, adding/removing/reordering/bypassing devices, and editing automation data where appropriate.
- Mark playback sync dirty on successful commands.
- Keep all new state serializable, even if later prompts fill in UI and engine usage.

Acceptance criteria:
- Existing tests continue to pass.
- New tests cover model invariants, default project creation, track-type behavior, master singleton behavior, route validation, device-chain ordering, mixer value ranges, automation curve validation/query, and command undo/redo.
- Existing project behavior remains intact: a new app session still starts with one MIDI track named `MIDI 1` unless product initialization is intentionally expanded.
- No Tracktion types leak into `tsq_core`.
```

---

## Prompt 02 — Project Serialization, Package Migration, And Backward Compatibility

```text
Task: Upgrade `.tseq` project package serialization for the new mixer architecture while remaining backward-compatible with existing projects.

Implement:
1. A project schema version bump with deterministic migration from the current project format.
2. Migration from legacy single assigned-instrument track fields into a device-chain model with one instrument slot where applicable.
3. Persistence for track type, mixer state, mute/solo, routing, sends, device chains, bypass states, plugin identifiers, plugin state-file references, automation curves, return tracks, master track, and audio track clip references.
4. Project package handling for audio assets and waveform-cache metadata. Do not copy large assets unnecessarily unless the existing package model expects imported assets to be embedded; make this policy explicit and test it.
5. Robust load behavior for missing plugins, missing audio files, missing device state files, invalid automation targets, invalid routes, and outdated schema fields.
6. User-facing diagnostics and warnings for recoverable load issues.

Instructions:
- Preserve existing persistence for tracks, clips, notes, custom scales, tempo map, time signature map, harmonic regions, chord regions, rhythm settings, plugin references, and clip harmonic metadata.
- Do not break old `.tseq` packages.
- Use explicit migration functions rather than scattered conditional parsing.
- Ensure migrated projects save back in the new format.

Acceptance criteria:
- Round-trip tests for new mixer projects.
- Migration tests using representative legacy project JSON.
- Missing-plugin and missing-audio tests that verify warnings are surfaced without preventing project load.
- Existing serialization tests remain green.
- Handoff includes the new schema shape and migration notes.
```

---

## Prompt 03 — Plugin Registry Upgrade: Instruments And Audio Effects

```text
Task: Upgrade plugin discovery, metadata, caching, and app services so VST3 audio effects are first-class citizens alongside instruments.

Implement:
1. Plugin registry metadata that distinguishes instruments, audio effects, and any ambiguous plugin capabilities discovered by JUCE/Tracktion.
2. Scanner support for VST3 audio effects, not only instruments.
3. Cached metadata migration from the current instrument-centric cache format.
4. Parameter discovery for plugins where safe and practical: stable parameter IDs/names/ranges/units/default values for automation display. Do not instantiate plugins on the audio thread.
5. Browser-facing filters/search fields for Instruments, Audio Effects, and All Plugins.
6. Diagnostics that report scan failures, unsupported formats, plugin crashes/dead-man-pedal events, and ambiguous plugin classifications.
7. API surfaces for assigning plugins to device chains by stable registry identity.

Instructions:
- Plugin scanning must remain on a worker thread.
- The audio thread must never scan or inspect plugin files.
- Preserve existing VST3 instrument assignment behavior and the hardcoded test-phrase path if still used.
- Existing plugin state restore behavior must remain compatible with migrated device chains.

Acceptance criteria:
- Tests or integration checks cover instrument and effect metadata caching, cache migration, scan failure diagnostics, and registry lookup by stable ID.
- Existing plugin browser behavior still works for instruments.
- New metadata supports later automation and Browser Panel prompts.
```

---

## Prompt 04 — Engine Playback Graph: Device Chains, Gain/Pan, Mute/Solo, Effects, Returns, And Master

```text
Task: Upgrade the playback engine adapter so the core mixer model produces real audio through a complete Tracktion-backed graph.

Implement engine support for:
1. Linear device chains on tracks: MIDI source -> optional MIDI-capable devices if supported -> instrument -> audio effects -> track output. For audio/return/master tracks, process audio effects in order.
2. VST3 audio effects loaded from the upgraded registry, including state restore and bypass.
3. Track volume, pan, mute/activator, and solo behavior. Solo must behave predictably with multiple soloed tracks and returns.
4. Return tracks with sends and return device chains.
5. A dedicated master track/device chain that processes the final mix before audio output.
6. Validated routing: Audio To, Audio From, MIDI To, MIDI From, master out, return sends, and sidechain-ready routing references where Tracktion support exists.
7. Realtime-safe level metering taps for every visible track, return, and master.
8. Robust stop/rebuild behavior that avoids stuck notes, stale plugin state, and transport desynchronization.

Instructions:
- Keep Tracktion details inside `tsq_engine` or the existing playback wrapper. Do not leak Tracktion into core/UI.
- Prefer immutable render/playback snapshots built on the message/control thread.
- All gain, pan, send, bypass, and automation-affectable controls must be smoothed or otherwise click-free.
- Do not fake meters. Meters must reflect actual audio signal levels.
- On unsupported routing/device cases, surface deterministic warnings and choose a safe fallback rather than crashing.

Acceptance criteria:
- Existing playback still works for old MIDI instrument projects.
- Tests or app-enabled integration checks verify instrument -> effect chains, bypass, mute, solo, gain, pan, sends to return, master processing, and plugin state restore.
- Manual smoke test: create MIDI track with instrument + audio effect, hear effect; mute/solo works; master chain works; meters move with audio.
- Audio-thread audit updated with new realtime boundaries.
```

---

## Prompt 05 — Realtime Metering Infrastructure And Mixer Control Quality

```text
Task: Implement high-quality metering and mixer-control infrastructure shared by UI and engine.

Implement:
1. Per-track, return, and master meter streams with peak and optionally RMS values.
2. Meter ballistics: attack/release/decay behavior appropriate for a professional DAW UI.
3. Realtime-safe communication from engine/audio callback to UI: atomics, lock-free ring buffers, or double-buffered snapshots.
4. dB/gain conversion helpers with sensible ranges, `-inf` handling, precise text parsing, and clamping.
5. Pan-law helpers with documented behavior and consistent engine/UI interpretation.
6. Smooth parameter ramps for volume, pan, send amount, and bypass transitions where applicable.

Instructions:
- Do not allocate or lock on the audio thread.
- Do not log from the audio thread; aggregate diagnostics safely if needed.
- UI polling must be efficient and bounded.
- Meter values should not jump erratically or remain stuck after stop/rebuild.

Acceptance criteria:
- Unit tests for dB/gain conversion, pan mapping, meter decay behavior, clamping, and stopped transport reset.
- Engine integration smoke test confirms meters are driven by real audio, not UI timers or fake values.
- Existing diagnostics and playback tests remain green.
```

---

## Prompt 06 — Right-Docked Browser Panel: Plugins, Project Files, And Scales

```text
Task: Replace the right-side Scale Palette area with a unified right-docked tabbed Browser Panel while preserving all existing scale behavior.

Implement UI for:
1. Tab 1: Plugins & Project Files.
   - Search/filter UI.
   - Plugin sections or filters for Instruments, Audio Effects, and All Plugins.
   - Project files list for VSTs, audio files, and MIDI files where the app can safely enumerate/import them.
   - Scan/refresh/status affordances reused or migrated from the existing Plugin Browser overlay.
2. Tab 2: Scales.
   - Move the existing Scale Palette functionality into this tab without regressing built-in scales, custom scales, search, drag-to-scale-lane, replace-on-existing-region, and create-on-empty-lane behavior.
3. Drag-and-drop from the Browser Panel.
   - Instruments can be dragged onto timeline lanes or track headers.
   - Audio effects can be dragged onto device chains or compatible track targets.
   - Audio/MIDI files can be dragged to timeline/track targets if the corresponding core/engine support exists.
4. Status and diagnostics for scan/import/drop failures.

Instructions:
- Avoid duplicating scale palette logic. Refactor existing components cleanly.
- Preserve focus/keyboard behavior used by piano roll and timeline shortcuts.
- Keep UI responsive during scans and large file lists.
- Do not remove the existing overlay Plugin Browser until all equivalent functionality is available or intentionally redirected.

Acceptance criteria:
- Existing scale palette tests/manual behavior still pass inside the Scales tab.
- Browser filters show instruments and effects from the upgraded registry.
- Dragging a scale still edits scale/mode regions exactly as before.
- Drag payloads are strongly typed and validated at drop sites.
- Manual QA covers scan, search, scale drag, plugin drag, and failure messages.
```

---

## Prompt 07 — Horizontal Mixer Track Headers

```text
Task: Upgrade track headers into a compact horizontal mixer while preserving current track-list behavior.

Implement per-track header controls:
1. Volume fader/slider plus precise dB text field.
2. Pan slider or rotary control with clear center indication.
3. Track Activator/Mute toggle.
4. Solo toggle with correct multi-solo behavior.
5. Realtime level meter using the actual metering infrastructure.
6. Routing dropdowns for Audio To, Audio From, MIDI To, and MIDI From, showing only valid choices for the selected track type.
7. Preserve existing track name, assigned instrument/device summary, ARM chips for MIDI recording, selection behavior, and timeline alignment.

Instructions:
- All mixer changes must use core commands and support undo/redo where appropriate.
- Text field entry must parse dB precisely, clamp safely, and show useful validation errors.
- Routing dropdown choices must come from the core routing model and reject invalid/cyclic routes.
- UI must remain responsive with many tracks.
- Do not fake meters; bind to real meter snapshots.

Acceptance criteria:
- Unit/UI-level tests where available for commands and value parsing.
- Manual QA: adjust volume/pan, type exact dB values, mute/solo multiple tracks, verify meters, change routing, undo/redo mixer changes.
- Existing track add/select/arm behavior remains intact.
```

---

## Prompt 08 — Track Creation, Empty-Area Context Menus, And Drag-To-Create Workflows

```text
Task: Implement professional track creation workflows in the timeline and track-list empty areas.

Implement:
1. Right-click context menus in empty timeline/track-list areas with:
   - Insert MIDI Track
   - Insert Audio Track
   - Insert Return Track
2. Drag-and-drop track creation:
   - Dropping an instrument VST3 into empty space below tracks creates a MIDI track, names it after the plugin, inserts the instrument into the device chain, selects the track, marks playback dirty, and surfaces status.
   - Dropping an audio-effect VST3 into empty track space creates an Audio Track with that effect in its device chain unless product rules in the existing codebase require a safer alternative; in either case, behavior must be deterministic and user-visible.
   - Dropping an audio file creates an Audio Track with an audio clip if audio-track support is already implemented by the relevant prompt.
   - Dropping a MIDI file creates a MIDI Track with imported clip data if MIDI import exists; otherwise surface a clear unsupported-operation message without crashing.
3. Correct insertion location based on drop position where practical.
4. Undo/redo for created tracks and assigned devices.

Instructions:
- Use command-backed mutations only.
- Validate plugin/file payloads before creating anything.
- Never create half-configured tracks. Use transactional commands or rollback on failure.
- Preserve timeline zoom, selection, marquee, and clip interactions.

Acceptance criteria:
- Tests cover insert-track commands and transactional drag-created tracks.
- Manual QA covers context menu insertion and plugin/file drag-to-create paths.
- Invalid drops produce clear messages and no partial project mutation.
```

---

## Prompt 09 — Lower Detail Area Toggle And Device Chain View

```text
Task: Add a lower-pane Device Chain view and a fast toggle between Piano Roll and Device Chain without degrading existing piano-roll workflows.

Implement:
1. A lower-pane mode model: Piano Roll vs Device Chain.
2. Keyboard shortcut such as Shift+Tab and a visible UI switch to toggle modes.
3. Device Chain view for the selected track:
   - Left-to-right serial signal flow: MIDI -> Instrument -> Audio FX for MIDI tracks; Audio -> Audio FX for audio tracks; Return Input -> FX for returns; Mix -> Master FX for master.
   - Device faceplates showing bypass/enable toggle, plugin name, plugin kind, parameter summary if available, and button to open the full floating VST3 GUI.
   - Clear empty-chain affordances.
4. Selection synchronization: selecting a track updates the Device Chain view; opening a MIDI clip can switch or remain in Piano Roll according to existing UX, but must never lose selection state.
5. Safe editor spawning for plugin GUIs with lifetime management.

Instructions:
- Preserve all current piano-roll features, shortcuts, zoom, note editing, scale-aware lanes, and harmonic overlays.
- Bypass and device edits must use commands.
- Floating plugin editor windows must not outlive destroyed plugin instances or project reloads.
- UI should not instantiate plugins on paint/layout paths.

Acceptance criteria:
- Manual QA: Shift+Tab toggles reliably; piano roll still works; selected track device chain displays accurately; bypass changes audio; plugin editor opens/closes safely.
- Existing piano-roll tests and manual checklist remain green.
```

---

## Prompt 10 — Device Chain Drag/Drop, Hot-Swapping, Reordering, And State Safety

```text
Task: Implement polished device-chain editing workflows.

Implement:
1. Drag plugin from Browser Panel into Device Chain to append at a valid location.
2. Drag plugin onto an existing device faceplate to replace/hot-swap.
3. Drag between devices to insert at a specific point.
4. Reorder devices within a chain where valid.
5. Remove device command and UI affordance.
6. Device replacement preserves user intent: optionally migrate compatible state only when safe; otherwise keep old state recoverable through undo.
7. Undo/redo for append, replace, remove, bypass, and reorder.
8. Diagnostics for invalid chains: effect before instrument if unsupported, second instrument conflicts, missing plugin, failed instantiation, incompatible track type.

Instructions:
- Do not silently discard plugin state. Device-slot state must be owned, serialized, and undoable.
- Hot-swap must be transactional: if the new plugin fails to load, the old device remains intact.
- Keep UI and engine snapshots coherent after chain edits.
- Avoid expensive plugin instantiation from UI drag hover paths.

Acceptance criteria:
- Unit tests for command undo/redo and chain validation.
- Integration/manual QA: append effect after instrument, replace instrument, reorder effects, bypass, remove, undo each step, save/reload and verify state.
```

---

## Prompt 11 — Audio Tracks, Audio File Import, Playback, And Waveform Rendering

```text
Task: Implement dedicated audio tracks with waveform rendering and playback that complement the existing MIDI architecture.

Implement:
1. Core audio-track and audio-clip model if not already completed: source file reference, clip start, length, source offset, gain if appropriate, loop/stretch flags only if supported correctly, and serialization.
2. Audio file import from Browser Panel and OS drag/drop where supported.
3. Audio playback through the same track mixer, routing, returns, and master chain as MIDI-generated audio.
4. Waveform overview rendering for audio clips in the timeline.
5. Waveform cache management: background analysis, invalidation, project package metadata, and safe missing-file behavior.
6. High-quality resampling/timebase handling according to Tracktion/JUCE best practices. Do not degrade audio fidelity with low-quality ad hoc conversion.
7. Diagnostics and user-facing errors for unsupported formats, missing files, decode errors, and sample-rate mismatches.

Instructions:
- Do not implement audio recording unless explicitly scoped elsewhere. This prompt is playback/import/waveform.
- Waveform generation must not block the audio thread or UI for large files.
- Audio clips must follow existing timeline selection, drag, resize, copy/paste, delete, snapping, and undo conventions where applicable.
- If editing operations are intentionally limited, make unavailable operations clear in the UI rather than silently doing nothing.

Acceptance criteria:
- Tests cover audio source references, serialization, missing-file load behavior, clip commands, and waveform cache metadata.
- Manual QA: import audio file, see waveform, play through track effects/master, move/resize clip, save/reload, handle missing file gracefully.
```

---

## Prompt 12 — Return Tracks, Sends, Bussing, Sidechain-Ready Routing, And Master Track UI

```text
Task: Complete return/master track architecture and expose it coherently in the UI.

Implement:
1. Return track creation, naming, ordering, device chain, meter, solo/mute behavior, and routing to master or valid outputs.
2. Track send controls to return tracks. Use smooth send gain changes and prevent feedback cycles.
3. Master track visibility with complete mixer strip, meter, and device chain for mastering effects.
4. Routing menus that expose Audio To, Audio From, MIDI To, MIDI From, sends, and sidechain-capable destinations where supported.
5. Clear route validation and cycle detection surfaced to the user.
6. Persistence and undo/redo for return/master changes.

Instructions:
- Return tracks and master must behave like first-class tracks where musically appropriate, but enforce singleton master and route constraints.
- Solo logic must account for tracks feeding returns; solo should not accidentally mute necessary return paths unless intentional and documented.
- Avoid routing graph rebuild glitches; stop/rebuild must be safe and deterministic.

Acceptance criteria:
- Tests for return creation, send values, route validation/cycles, master singleton, serialization, and solo logic.
- Manual QA: create return with reverb/delay effect, send from a MIDI/audio track, adjust send, solo track with return audible as expected, insert master effect, save/reload.
```

---

## Prompt 13 — Automation Core, Engine Binding, And UI Lanes

```text
Task: Implement expandable vector automation lanes for volume, panning, sends, and exposed VST3 parameters.

Implement:
1. Core automation lane model and commands if not already completed: create/show/hide lane, add/move/delete points, change interpolation, clear lane, and bind target.
2. Timeline UI expandable lanes beneath tracks.
3. Drawing/editing interactions: click to add point, drag point, marquee/select points if consistent with existing selection system, snap to grid, copy/paste if feasible, delete, undo/redo.
4. Targets: track Volume, Pan, Send Level, and exposed VST3 parameters for devices in the selected track's chain.
5. Engine playback binding: automation values must affect actual audio/plugin parameters at the correct musical time.
6. Smoothing and timing policy: avoid zipper noise and document whether automation is sample-accurate, block-accurate, or Tracktion-managed.
7. Robust target invalidation: deleting/replacing devices must gracefully disable, retarget, or preserve automation according to a deterministic policy.

Instructions:
- Do not draw decorative automation that does not drive audio.
- Do not update plugin parameters from the UI thread during playback except through safe engine/control mechanisms.
- Parameter ranges and display values must come from plugin metadata when available.
- Automation data must serialize and migrate safely.

Acceptance criteria:
- Tests for automation curve interpolation/query, command undo/redo, serialization, target invalidation, and playback snapshot generation.
- Manual QA: automate volume and pan; automate a VST3 parameter; save/reload; delete/reinsert device and verify deterministic automation behavior; verify no clicks/zipper noise in normal use.
```

---

## Prompt 14 — Browser Project Files: Audio And MIDI Import Polish

```text
Task: Finish project-file browsing and import workflows for audio and MIDI files.

Implement:
1. Browser Panel file discovery for project package files, imported assets, and optionally user-selected folders if the app settings model supports it.
2. Audio preview if feasible without compromising scope; otherwise provide clear metadata and drag-to-import.
3. MIDI file import to a MIDI track/clip using existing tempo/time-signature behavior carefully.
4. File-type icons/labels and search filtering.
5. Missing/unsupported/corrupt file diagnostics.
6. Persistence for imported asset references and package relocation behavior.

Instructions:
- Avoid blocking UI while indexing large folders.
- MIDI import must preserve musical timing; do not flatten tempo/meter incorrectly.
- Audio import must respect sample rate/channel count and use high-quality engine paths.
- Keep this integrated with drag-to-create and drag-onto-existing-track workflows.

Acceptance criteria:
- Tests for MIDI import timing where practical, asset reference serialization, and unsupported-file diagnostics.
- Manual QA: import MIDI and audio by dragging from Browser Panel; save/reload project; move package if supported; verify files resolve or warn clearly.
```

---

## Prompt 15 — UI Cohesion, Accessibility, Keyboard Workflow, And Visual Polish

```text
Task: Polish the integrated mixer/browser/device/automation UI so it feels like a professional DAW rather than bolted-on panels.

Implement/refine:
1. Consistent sizing, spacing, typography, hit targets, hover states, and focus rings for Browser Panel, track headers, Device Chain, automation lanes, and meters.
2. Keyboard workflow: Shift+Tab lower-pane toggle, tab navigation where appropriate, Escape behavior for popups/editors, and preservation of existing piano-roll/timeline shortcuts.
3. Responsive behavior with many tracks and narrow window sizes.
4. Clear empty states: no track selected, no devices, no plugins scanned, no project files, no automation target available.
5. Accessible names/tooltips for mixer controls, meters, routing menus, device bypass, plugin editor buttons, and automation targets.
6. Diagnostic/status messages that are concise and actionable.

Instructions:
- Do not hide missing engine functionality behind pretty UI. Every visible control must either work or be clearly unavailable with an explanation.
- Avoid per-paint allocations and expensive layout work.
- Preserve existing harmonic header, timeline, status bar, overlay panels, and lower piano-roll workflows.

Acceptance criteria:
- Manual UX pass covering common workflows from an empty project through plugin scan, track creation, device chain, automation, routing, save/load, and playback.
- No regressions in existing timeline/piano-roll/scale workflows.
```

---

## Prompt 16 — Performance, Realtime Safety, And Audio Quality Hardening

```text
Task: Audit and harden the full mixer implementation for professional DAW audio quality and performance.

Do a code-level audit of:
1. Audio thread: no locks, allocations, file IO, plugin scans, logging/string formatting, UI calls, or unbounded work.
2. Engine snapshot/rebuild boundaries: ensure project edits, plugin state changes, automation, and routing updates are synchronized safely.
3. Gain/pan/send/automation smoothing: verify no zipper noise or clicks in normal use.
4. Metering: verify bounded, realtime-safe, accurate signal data and clean reset behavior.
5. Plugin lifetime: editor windows, state restore, device replacement, project reload, transport stop/start, and crash/failure behavior.
6. Large projects: many tracks, many automation points, long audio files, many devices, and large waveform caches.
7. Serialization/package IO: no accidental audio-thread access; no excessive blocking in UI-critical paths.
8. Cross-platform build assumptions for macOS, Windows, Linux where this repo supports them.

Instructions:
- Fix issues found during the audit, not just document them.
- Add tests or debug assertions for invariants that could regress.
- Update the audio-thread audit documentation.
- Prefer measured improvements over micro-optimizations. Do not compromise audio fidelity for premature speed gains.

Acceptance criteria:
- Relevant automated tests pass.
- Manual stress test checklist completed.
- Audio-thread audit updated with new mixer/device/automation paths.
- Handoff lists measured or reasoned performance risks and mitigations.
```

---

## Prompt 17 — Complete Regression Test Suite And Manual QA Checklist

```text
Task: Bring the entire feature set to a releasable quality bar with automated tests, integration checks, and manual QA documentation.

Implement/update tests for:
1. Core models: track types, mixer strip, device chain, routing, sends, master/return tracks, audio clips, automation curves.
2. Commands: create/delete tracks, mixer edits, route edits, device append/replace/reorder/remove/bypass, automation edits, audio clip edits.
3. Serialization: new schema, migration from old schema, missing plugins, missing audio files, invalid automation targets, route cycles.
4. Plugin registry: instruments/effects, cache migration, scan failure diagnostics, parameter metadata.
5. Engine integration where available: instrument + effects chain, mute/solo, gain/pan, return sends, master chain, automation playback, real meters.
6. UI/app smoke tests where available: Browser Panel, Scale tab regression, track header controls, context menus, Device Chain view, automation lanes, audio import/waveform.
7. Existing features: piano roll, scale-aware lanes, custom scales, theory tools, MIDI recording/input transforms, project save/load, plugin state persistence, MIDI export, diagnostics.

Manual QA checklist must include:
- Launch new project.
- Scan plugins; verify instruments and effects.
- Create MIDI track by context menu and by instrument drag.
- Add instrument and audio effects to device chain.
- Open floating plugin GUI, edit parameter, save/reload, verify state.
- Play MIDI clip through effects; verify volume/pan/mute/solo/meters.
- Create audio track from audio file; verify waveform and playback.
- Create return track, add reverb/delay, send from track, verify routing and solo behavior.
- Add master effect and verify final output processing.
- Draw volume, pan, send, and plugin-parameter automation; verify audible effect and save/reload.
- Drag scales in Scales tab; verify old scale-palette behavior remains.
- Test missing plugin/audio file recovery.
- Test undo/redo across all new workflows.

Instructions:
- Do not mark complete with failing tests unless the failures are unrelated and explicitly justified with evidence.
- Update docs for known limitations only if a limitation genuinely remains. Do not use docs to excuse incomplete implementation.
- Produce a release-readiness summary and a list of any remaining risks.

Acceptance criteria:
- Automated tests pass or failures are fully explained with exact evidence.
- Manual QA checklist is complete and saved in the repo.
- The product can perform the full Ableton-style mixer workflow end to end.
```

---

## Prompt 18 — Final Product Integration Review

```text
Task: Perform the final uncompromising integration review for the Ableton-style mixer upgrade.

Review the implementation end to end against these requirements:
1. Right-docked tabbed Browser Panel with Plugins & Project Files and Scales tabs.
2. Existing Scale Palette functionality fully preserved inside the Scales tab.
3. Drag-and-drop instruments to timeline/track headers and empty track space.
4. Horizontal mixer track headers with volume, dB text input, pan, mute/activator, solo, meters, and routing menus.
5. Context menus for Insert MIDI Track, Insert Audio Track, Insert Return Track.
6. Lower-pane Piano Roll / Device Chain toggle, including Shift+Tab.
7. Device chain UI with serial signal flow, bypass, plugin name, floating editor button, hot-swap/append/replace workflows.
8. VST3 audio effect scanning, hosting, state persistence, and playback.
9. Automation lanes for volume, pan, and exposed VST3 parameters, driving real audio/plugin parameters.
10. Return tracks, sends, bussing/sidechain-ready routing, and master track with device chain.
11. Audio tracks with waveform rendering and playback.
12. Project save/load/migration for all new state.
13. Audio quality and realtime safety: no avoidable clicks, no fake meters, no audio-thread blocking, no state races.
14. Existing DAW/theory features remain intact.

Instructions:
- Verify every requirement by reading code and running tests/manual checks where possible.
- Fix any small integration gaps found.
- For larger gaps, create precise follow-up issues only after making the best effort to complete them.
- Update `FEATURES.md` or equivalent implementation inventory to accurately reflect what is now shipped.
- Update known issues only with truthful remaining issues.

Acceptance criteria:
- Final handoff contains: requirement-by-requirement status, tests run, manual QA status, remaining risks, and exact files/docs updated.
- No requirement is marked complete unless it has real model, UI, engine, persistence, and/or test support as appropriate.
```

---

# Suggested Execution Order

Run the prompts in order. Do not skip foundation prompts unless the repository already contains equivalent completed code verified by tests.

1. Prompt 00 — audit/map.
2. Prompts 01-03 — core model, persistence, plugin registry.
3. Prompts 04-05 — engine graph and metering.
4. Prompts 06-10 — browser, mixer UI, track creation, device chain, hot-swap.
5. Prompts 11-14 — audio tracks, returns/master, automation, file import.
6. Prompts 15-16 — UI polish and realtime/audio-quality hardening.
7. Prompts 17-18 — regression, QA, and final integration review.

# Minimal Handoff Template For The Agent

Each prompt should end with this exact handoff shape:

```text
Handoff Summary
- Completed:
- Files changed:
- Architecture decisions:
- Tests run:
- Test results:
- Manual QA performed:
- Known risks:
- Follow-up needed:
- Recommended next prompt:
```
