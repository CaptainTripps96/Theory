# Ableton-Style Mixer Implementation Map

Last updated: 2026-06-09

This document is the Prompt 00 audit artifact for `docs/agent/TheorySequencer_Ableton_Mixer_Prompt_Pack.md`. It maps the current repository to the planned Ableton-style mixer upgrade and identifies where each later prompt should make changes. It is intentionally audit-only; no product implementation is claimed complete here.

## Current Architecture Snapshot

TheorySequencer is a C++20/JUCE/Tracktion Engine desktop app with these active boundaries:

- `src/core`: product data model, musical theory, sequencing, commands, serialization, MIDI export, diagnostics-safe result types. This layer must remain free of JUCE UI and Tracktion dependencies.
- `src/app`: app service ownership, project lifecycle, settings, diagnostics plumbing, MIDI input recording, plugin registry/scanner ownership, playback dirtying, and bridge methods used by UI.
- `src/engine`: Tracktion-backed playback and VST3 hosting behind `PlaybackEngine`. Tracktion details are contained here.
- `src/ui`: JUCE components for transport, timeline, track list, scale palette, piano roll, plugin browser overlay, audio settings, diagnostics, and status bar.
- `tests/core`: pure Catch2 tests for core model, commands, serialization, theory, MIDI export, recording transforms, and diagnostics.
- `tests/integration`: app-enabled Tracktion/VST3 regression harness, currently focused on preserving Synthesizer plugin state.

The custom core project model is the source of truth. Tracktion is currently a playback/plugin-hosting backend that materializes a prepared edit from the core project before playback.

## Source Map

### Core Sequencing Model

- `src/core/sequencing/Project.h/.cpp`: owns project metadata, musical structure, tempo/time-signature maps, rhythm settings, MIDI tracks, and custom scales. Mixer work should add project-owned master/return state and any global mixer/routing collections here or in closely related core classes.
- `src/core/sequencing/Track.h/.cpp`: currently represents a MIDI track only. It owns a name/id, optional `TrackInstrumentReference`, and MIDI clips. Prompt 01 should evolve this into typed tracks or a track aggregate with MIDI/audio/return/master behavior while preserving existing MIDI track calls.
- `src/core/sequencing/MidiClip.h/.cpp`: existing MIDI clip source/arrangement length, loop state, notes, and clip harmonic metadata. Preserve unchanged where possible.
- `src/core/sequencing/TrackInstrumentReference.h/.cpp`: legacy one-instrument-per-track plugin reference. Prompt 01 should either migrate this into a more general device reference type or keep it as a compatibility adapter over a device chain until Prompt 02 migrates persistence.
- `src/core/sequencing/MusicalStructure.*`, `KeyCenterRegion.*`, `ScaleModeRegion.*`, `ChordProgressionLane.*`, `ChordRegion.*`: existing harmonic lanes that must not regress while the mixer lands.
- `src/core/time/*`: PPQ tick, grid, tempo, and meter primitives. Automation points, audio clips, and waveform/cache metadata should use these tick types rather than raw integers.

Recommended new core files for Prompt 01:

- `src/core/sequencing/TrackType.h`
- `src/core/sequencing/MixerStrip.h`
- `src/core/sequencing/DeviceChain.h`
- `src/core/sequencing/DeviceReference.h`
- `src/core/sequencing/Routing.h`
- `src/core/sequencing/Automation.h`
- `src/core/sequencing/AudioClip.h`
- `src/core/sequencing/MixerValidation.h`

### Command Layer

- `src/core/commands/Command.h`: command interface with execute/undo returning `diagnostics::Result`.
- `src/core/commands/CommandStack.h/.cpp`: undo/redo stack and change callback. `AppServices` uses the callback to mark playback dirty.
- `src/core/commands/ProjectMutationCommands.cpp`: existing command implementations. It is already large; mixer work may justify moving new commands into focused `.cpp` files instead of expanding this file indefinitely.
- Existing command headers include track, clip, note, harmonic region, custom scale, rhythm, transpose, chord, and instrument assignment commands.

Recommended new command families:

- `AddTrackCommand` should either be extended to accept a track type or complemented by `AddAudioTrackCommand`, `AddReturnTrackCommand`, and master-safe project initialization helpers.
- Mixer state commands: set track name/color, volume, pan, mute/activator, solo, send value.
- Routing commands: set audio/MIDI source/destination, validate sends, prevent cycles.
- Device-chain commands: add, remove, replace, reorder, bypass device.
- Automation commands: add/move/delete point, clear lane, change interpolation, bind/unbind target.
- Audio clip commands: add/move/resize/delete audio clips once audio tracks exist.

### Serialization And Project Packages

- `src/core/serialization/ProjectSerializer.cpp`: writes/reads schema v1 `project.json`. Tracks currently serialize as `{ id, name, clips, pluginReference }`.
- `src/core/serialization/ProjectSchemaVersion.h`: current project schema version.
- `src/core/serialization/ProjectMigration.cpp`: currently rejects any schema other than current. Prompt 02 must turn this into a real migration pipeline.
- `src/core/serialization/ProjectPackage.cpp`: creates `.tseq` folders with `project.json`, `plugin-states`, `assets`, and `exports`.
- `src/core/serialization/JsonHelpers.*`: core-owned JSON helper; continue using it unless a deliberate dependency change is approved.

Mixer serialization needs:

- Schema bump with deterministic migration from legacy track `pluginReference` into a first device-chain instrument slot.
- Track type, mixer strip, routing, sends, device chains, automation lanes, return tracks, master track, and audio clip references.
- Plugin-state file paths per device slot rather than only per track.
- Audio asset/waveform metadata policy under `assets` and a future waveform cache area.
- Recoverable load warnings surfaced through app services for missing plugins, invalid routes, invalid automation targets, missing audio files, and missing state files.

### Engine And Playback

- `src/engine/PlaybackEngine.h`: control-thread abstraction. It currently exposes audio devices, project sync, playback, playhead, project loop, test instrument APIs, plugin-state capture, and plugin-state diagnostics.
- `src/engine/TracktionPlaybackEngine.cpp`: owns the Tracktion engine/edit, VST3 hosting, plugin editor windows, project sync, MIDI clip materialization, plugin state capture/restore, transport, loop, playhead, and debug plugin-parameter watchdog.
- `src/engine/EngineTypes.h`: simple app-facing engine value types. This is the natural home for future meter snapshots, device-chain engine warnings, and plugin-state capture keyed by device slot.

Current playback boundary:

- `AppServices::syncPlaybackProjectIfNeeded()` calls `PlaybackEngine::syncProject(project)` when the command stack marks the project dirty.
- `TracktionPlaybackEngine::syncProject()` rebuilds or updates a Tracktion edit on the control/message path.
- MIDI clips and notes are materialized into Tracktion clips before playback; the audio callback consumes the prepared Tracktion edit.

Engine work later must:

- Materialize typed tracks, device chains, mixer state, routing, sends, returns, master, audio clips, and automation from immutable or bounded project snapshots.
- Preserve plugin-state work that was added for the persistent Synthesizer reset bug.
- Avoid exposing Tracktion types outside `src/engine`.
- Add real, audio-driven metering without fake UI timers.
- Keep filesystem, plugin scans, heavy JSON parsing, and logging out of render paths.

### Plugin Registry And Scanning

- `src/engine/plugins/PluginDescription.h`: JUCE-independent plugin metadata currently includes `isInstrument`, channel counts, file identifiers, and numeric IDs.
- `src/engine/plugins/PluginRegistry.h/.cpp`: XML cache guarded by a mutex. Schema is local to the plugin cache and currently versioned as 1.
- `src/engine/plugins/PluginScanService.h/.cpp`: VST3 scan worker thread using JUCE plugin scanning; writes the registry cache and scan status.
- `src/ui/PluginBrowserComponent.*`: overlay UI that lists cached plugins, scans VST3s, assigns instruments to tracks, plays/stops a test phrase, and opens editor.

Needed upgrade:

- Distinguish instruments, audio effects, MIDI effects if supported, and ambiguous plugins.
- Cache automation-useful parameter metadata when safe.
- Keep plugin scanning/inspection on worker/control paths, never audio thread.
- Preserve existing instrument assignment while introducing device-chain assignment.
- Migrate plugin cache schema without losing existing entries.

### App Services

- `src/app/AppServices.h/.cpp`: owns project, command stack, settings, playback engine, plugin registry/scan service, MIDI recording service, diagnostics, project save/load, plugin-state file capture/write, missing-plugin warnings, and playback dirty flag.
- `AppServices::assignInstrumentToTrack()` currently validates `plugin.isInstrument`, executes `AssignTrackInstrumentCommand`, and syncs playback.
- `AppServices::projectPreparedForSave()` and `writePluginStateFiles()` currently key plugin state by track id.
- `AppServices::warnAboutMissingPlugins()` checks legacy track instruments against the plugin registry.

App service changes later:

- Device-chain assignment APIs and drag/drop transaction helpers.
- Plugin state capture/write keyed by stable device-slot ID.
- Missing plugin/audio/automation/routing diagnostics during load.
- Track creation helpers for MIDI/audio/return/master-safe operations.
- Meter snapshot and engine warning APIs for UI.
- Keep direct project mutation out of UI paths; use command stack.

### UI

- `src/ui/MainComponent.*`: lays out transport, track list, timeline, right-side `ScalePaletteComponent`, lower `DetailEditorComponent`, status bar, and overlay panels. It owns global shortcuts and project menu actions.
- `src/ui/TransportComponent.*`: playback, loop, MIDI input, record, quantize, scale lock, Audio/Plugins/Project buttons.
- `src/ui/TrackListComponent.*`: left track list, track name, instrument summary, and ARM chip. It is the likely host for horizontal mixer track headers or a replacement component.
- `src/ui/TimelineComponent.*`: arrangement grid, musical structure lanes, clip editing, marquee selection, scale drag target, playhead, `+ Track`, `Globalize Chords`, and timeline contextual grid.
- `src/ui/ScalePaletteComponent.*`: current right-side scale palette with search, custom scale editor, and drag-to-scale-lane payloads. Prompt 06 should preserve this behavior by moving/refactoring it into a Browser Panel tab.
- `src/ui/PluginBrowserComponent.*`: current plugin overlay; should remain until the Browser Panel reaches feature parity.
- `src/ui/DetailEditorComponent.*`: wraps the piano roll. Prompt 09 should become a lower-pane mode controller for Piano Roll vs Device Chain.
- `src/ui/PianoRollComponent.*`: extensive scale-aware note editor. Preserve focus, shortcuts, selection, paste playhead, clip length/loop, grid, tuplets, transpose, scalar fill, and harmonic headers.
- `src/ui/DiagnosticsComponent.*`, `StatusBarComponent.*`, `AudioSettingsComponent.*`: preserve and extend diagnostics/status/audio behavior.

Recommended new UI components:

- `BrowserPanelComponent`: right-docked tab host.
- `PluginAndFilesBrowserComponent`: plugin filters/search/project files tab.
- `ScalesBrowserTabComponent`: wrapper/refactor of existing `ScalePaletteComponent`.
- `MixerTrackHeaderComponent` or an upgraded `TrackListComponent`: volume, pan, mute/activator, solo, routing, meter, ARM, device summary.
- `DeviceChainComponent`: lower-pane serial device chain.
- `AutomationLaneComponent`: expandable vector automation lanes.
- `MeterComponent`: reusable UI meter bound to real meter snapshots.
- Context-menu helpers or payload types for strongly typed drags.

### Tests

- `tests/core/ProjectModelTest.cpp`: track/clip invariants and overlap behavior.
- `tests/core/CommandStackTest.cpp`: command undo/redo coverage.
- `tests/core/ProjectSerializationTest.cpp`: project round trips, package files, plugin reference persistence, invalid schema behavior.
- `tests/core/RecordingInputTransformTest.cpp`, `TimeModelTest.cpp`, theory tests: must remain green.
- `tests/integration/VstStateRegressionTest.cpp`: real Synthesizer VST3 plugin-state regression; must not be broken by device-chain migration.
- `tests/CMakeLists.txt`: add new pure core test files here; integration tests should remain optional under `TSQ_BUILD_APP`.

Recommended new tests:

- `MixerModelTest.cpp`
- `RoutingValidationTest.cpp`
- `DeviceChainTest.cpp`
- `AutomationCurveTest.cpp`
- `AudioClipModelTest.cpp`
- `MixerCommandTest.cpp` or expanded command tests.
- `MixerSerializationTest.cpp` and migration fixtures.
- `PluginRegistryTest.cpp` if plugin cache migration can be tested without a live scan.
- Engine integration tests for device chain/effects/meters after engine prompts.

## Feature-To-Code Mapping

### Track Types: MIDI, Audio, Return, Master

- Core: `Track`, new `TrackType`, project-level master/return invariants.
- Commands: typed track creation, delete rules, master singleton enforcement.
- Serialization: track type, master/return persistence and migration from legacy MIDI tracks.
- Engine: map track type to Tracktion audio/MIDI/return/master constructs.
- UI: track headers/list and context menus.
- Tests: model invariants, command undo/redo, serialization.

### Mixer Strip State

- Core: `MixerStrip` with volume dB/gain, pan, mute/activator, solo, color/name, meter source ID.
- Commands: set volume, pan, mute, solo, color/name.
- Engine: apply smoothed gain/pan/mute/solo; solo behavior across returns/master.
- UI: horizontal track header controls and dB text parsing.
- Tests: value ranges, dB/gain conversion, command undo/redo, engine/manual checks.

### Routing And Sends

- Core: typed route references for Audio From/To, MIDI From/To, sends, master, sidechain-ready refs; validation/cycle prevention.
- Commands: set routes and sends transactionally.
- Serialization: route references and send levels.
- Engine: materialize route graph and safe fallbacks for unsupported cases.
- UI: routing dropdowns with valid choices only.
- Tests: cycle validation, invalid route diagnostics, serialization, solo/return behavior.

### Device Chains

- Core: stable device-slot IDs, plugin references, kind metadata, bypass, ordering, state-file association.
- Commands: append, insert, replace, remove, reorder, bypass.
- Serialization: device-chain slots and per-slot state files.
- Plugin registry: stable plugin identities and parameter metadata.
- Engine: load instrument/effect chains and preserve plugin state.
- UI: Browser drag/drop and Device Chain view.
- Tests: chain rules, undo/redo, migration from `TrackInstrumentReference`.

### VST3 Audio Effects

- Plugin registry: classify audio effects and ambiguous plugins.
- App services: assign plugins to device chains.
- Engine: instantiate and order effects after instruments or in audio/return/master chains.
- Serialization: preserve effect device slots and state.
- UI: Browser filters and device-chain drops.
- Tests: registry cache migration and integration/manual effect playback.

### Browser Panel

- UI: replace right-side scale-only region in `MainComponent` with tabbed Browser Panel.
- Preserve: `ScalePaletteComponent` search/custom scale/drag behavior.
- Plugin/files tab: reuse plugin scan status and registry data; add filters.
- App services: file discovery/import APIs as later prompts require.
- Tests/manual: scale drag regression, scan/filter/drop behavior.

### Horizontal Mixer Track Headers

- UI: upgrade `TrackListComponent` or split row rendering into a dedicated mixer header component.
- Core/commands: all edits command-backed.
- Engine: meters and control snapshots.
- App services: user warnings and meter access.
- Tests/manual: value parsing, undo/redo, multi-solo, routing.

### Track Creation Context Menus And Drag-To-Create

- UI: `TimelineComponent` and track-list empty space context menus/drop handling.
- Core/commands: typed add track plus transactional device/audio/MIDI clip creation.
- App services: helper functions to validate payloads and report failures.
- Tests/manual: invalid drop leaves no partial state.

### Lower Device Chain View

- UI: `DetailEditorComponent` becomes a mode host for `PianoRollComponent` and new `DeviceChainComponent`.
- Core/commands: device-chain edits and bypass.
- Engine: editor windows tied to live plugin instances safely.
- Preserve: piano roll selection, shortcuts, focus, and open clip state.

### Audio Tracks And Waveforms

- Core: audio source references, audio clips, source offsets, length, non-destructive flags, optional per-clip gain.
- Serialization/package: asset references and waveform cache metadata.
- Engine: Tracktion audio clip playback through mixer graph.
- UI: timeline waveform rendering and file drag/import.
- Worker/background: waveform analysis, invalidation, missing-file handling.
- Tests: source refs, serialization, missing-file behavior, clip commands.

### Return Tracks, Master, Bussing, Sidechain-Ready Routing

- Core: first-class return tracks, master singleton, sends, sidechain-ready route references.
- Engine: route/summing graph, return device chains, master chain, solo logic.
- UI: return/master rows, send controls, routing menus.
- Tests: master singleton, send values, feedback prevention, solo logic, serialization.

### Automation

- Core: automation targets and curves with tick positions, normalized values, interpolation, validation, query.
- Commands: lane and point editing.
- Serialization: automation lanes and target references.
- Engine: bind automation to gain/pan/send/plugin parameters with documented timing/smoothing.
- UI: expandable timeline lanes and target menus.
- Tests: interpolation/query, target invalidation, serialization, playback snapshot generation.

### Realtime Metering

- Engine: actual signal taps for tracks/returns/master; realtime-safe publication.
- Core/engine types: meter IDs/snapshots and ballistics helpers.
- UI: reusable meter component in track headers/master/returns.
- Tests: ballistics, stopped reset, dB/gain/pan helpers, integration for real signal.

## Dependency Ordering

1. Prompt 01: Core domain model and commands. This must land before UI or engine work so there is a stable source of truth.
2. Prompt 02: Serialization/migration. New state should become durable before major UI workflows depend on it.
3. Prompt 03: Plugin registry upgrade. Device chains and automation need plugin kind and parameter metadata.
4. Prompt 04: Engine playback graph. Actual audio behavior follows the core and registry model.
5. Prompt 05: Metering/control quality. UI meter controls should not be built on fake data.
6. Prompt 06: Browser Panel. It can safely expose plugin/effect/scales after registry support exists.
7. Prompt 07: Horizontal mixer track headers. Requires core commands, engine mixer state, and real meters.
8. Prompt 08: Track creation/context menus/drag-to-create. Requires typed tracks and device-chain transaction commands.
9. Prompt 09: Lower Device Chain view. Requires device-chain core and engine support for real bypass/editor behavior.
10. Prompt 10: Device-chain drag/drop/hot-swap. Requires Browser Panel and Device Chain view.
11. Prompt 11: Audio tracks/waveforms. Requires core audio placeholders and engine routing/mixer graph.
12. Prompt 12: Return/master UI and bussing. Requires routing/sends and engine graph stability.
13. Prompt 13: Automation. Requires stable targets from mixer/device/routing/plugin metadata.
14. Prompt 14: Project file import polish. Builds on audio/MIDI track import and Browser Panel.
15. Prompt 15: UI cohesion. Polish after all major surfaces exist.
16. Prompt 16: Performance/realtime/audio-quality hardening. Audit complete implementation, then fix.
17. Prompt 17: Regression and manual QA suite.
18. Prompt 18: Final integration review and feature inventory update.

## Existing Behavior To Preserve

- New app session starts with one MIDI track named `MIDI 1`.
- Existing MIDI tracks, clips, loop behavior, piano-roll editing, scale-aware lanes, accidental lanes, harmonic headers, scalar fill, transposition, grid/tuplet controls, clip length/loop controls, copy/paste/select-all, and timeline grid behavior.
- Scale palette search, custom scale creation, and drag-to-scale-lane behavior.
- Existing Plugin Browser overlay until the Browser Panel is feature-complete or explicitly redirected.
- Project save/load for current `.tseq` packages and plugin state files.
- VST3 instrument assignment and Synthesizer plugin-state preservation.
- MIDI recording/input quantization/Scale Lock.
- MIDI export for the open clip.
- Diagnostics log and status-bar warning flows.
- Command-backed undo/redo for user-visible project mutations.

## Audio-Thread And Realtime Boundaries

Current safe boundaries:

- `TracktionPlaybackEngine::syncProject()` is the prepared playback boundary. It traverses the core project, loads plugins, restores plugin state, configures tempo/meter, and creates Tracktion clips before playback.
- `MidiInputRecordingService::handleIncomingMidiMessage()` is realtime-adjacent and currently limits work to fixed-queue primitive event capture.
- `AppServices::processMidiRecordingEvents()` performs MIDI recording project mutations later on the message/control path.
- Plugin scanning runs in `PluginScanService` on a worker thread.
- Project package IO and plugin-state file IO happen on app/control paths, not render callbacks.

Risks introduced by the mixer upgrade:

- Meter publication must not lock or allocate on the audio callback.
- Gain/pan/send/bypass/automation changes need smoothing to avoid clicks and zipper noise.
- Engine graph rebuilds must not race plugin editor windows or observed plugin-state protection.
- Device-chain hot-swap must not leave stale Tracktion plugin pointers or orphaned editor windows.
- Automation must not drive plugin parameters from UI paint/timer paths.
- Waveform rendering and audio file analysis must remain off the audio thread and should not block UI for large files.
- Routing graph validation must happen before engine materialization to avoid runtime feedback loops.

## Risk Register

### Audio Quality

- Risk: abrupt volume, pan, send, bypass, or automation changes cause clicks.
- Mitigation: add dB/gain/pan helpers, smoothing/ramp policy, tests for conversion and clamp behavior, and engine-level ramps where Tracktion permits.

### Performance

- Risk: large projects with many tracks/devices/automation points make synchronous `syncProject()` slow.
- Mitigation: keep Prompt 01 data query-efficient, prepare bounded snapshots, and revisit worker/control preparation in Prompt 16 if sync cost grows.

### Persistence Migration

- Risk: legacy `.tseq` projects fail after schema bump.
- Mitigation: implement explicit v1-to-v2 migration tests with legacy JSON fixtures before broad serialization changes.

### Plugin State

- Risk: device-chain migration breaks the recent Synthesizer state-preservation fixes.
- Mitigation: keep integration regression active, key plugin states by stable device-slot IDs, and preserve live-state capture around project sync and transport operations.

### Routing Cycles

- Risk: sends/returns/sidechain-ready routes create feedback or unsupported Tracktion graphs.
- Mitigation: core route validation and cycle detection before commands commit, with deterministic warnings and safe fallbacks.

### Automation Timing

- Risk: automation curves look correct but do not drive real audio at the correct time.
- Mitigation: model automation targets in core, bind them in engine snapshots, document timing resolution, and add playback/integration checks.

### UI Regression

- Risk: Browser Panel, Device Chain, and mixer headers steal focus/shortcuts from timeline or piano roll.
- Mitigation: preserve `MainComponent` global shortcut behavior, test/manual check copy/paste/select-all, and keep piano-roll mode state stable.

### Audio Files And Waveforms

- Risk: missing/corrupt audio files or expensive waveform generation blocks UI or silently fails.
- Mitigation: explicit audio asset reference model, background waveform analysis, cache metadata, and recoverable diagnostics.

## Prompt 01 Readiness Notes

Prompt 01 should start in `src/core/sequencing` and `src/core/commands`, with tests in `tests/core`. It should not modify Tracktion engine internals or UI.

The minimum valuable Prompt 01 slice should include:

- Track type representation that keeps current `Track { "track-1", "MIDI 1" }` behavior valid.
- Mixer strip value types with range invariants and dB/gain helpers.
- Device-chain model with stable slot IDs and a compatibility path from the existing instrument reference.
- Route references and validation helpers with enough structure for future engine/UI prompts.
- Automation target and curve primitives with efficient query.
- Audio clip/source placeholder model, even if engine/UI do not use it yet.
- Commands for typed track creation, mixer state edits, route edits, device-chain edits, and automation edits where the model is ready.
- New core tests and CMake test registration.

Do not mark the Ableton-style mixer upgrade complete after Prompt 01. The following prompts still need persistence, registry, engine, metering, UI, audio tracks, returns/master, automation binding, import polish, hardening, and full QA.
