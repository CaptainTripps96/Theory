# TheorySequencer Feature Inventory

Last updated: 2026-06-10

This file lists the features that exist in the app and supporting codebase so far. It is based on the current docs, implementation log, UI source, core model, and test suite. Planned product-spec ideas are not listed as shipped features unless there is implementation evidence in the repo.

## App Shell And Architecture

- Standalone desktop application built with C++20, JUCE, and Tracktion Engine.
- Modular targets for `tsq_core`, `tsq_engine`, `tsq_ui`, `tsq_app`, and tests.
- App-owned `AppServices` container for project state, command stack, settings, playback, plugins, MIDI input, diagnostics, and file operations.
- Tracktion Engine is wrapped behind the app-owned `PlaybackEngine` interface so UI/core code do not depend directly on Tracktion internals.
- Cross-platform build documentation and CMake presets for macOS, Windows, Linux, and headless test builds.
- Default app session initializes a 120 BPM, 4/4, C Major project with one MIDI track named `MIDI 1`.

## Main Workspace

- Transport bar with Start, Stop, Return to Start, project playback Loop, MIDI Record, input Quantize, Scale Lock, MIDI input selector, Audio, Plugins, and Project controls.
- Central workspace with compact mixer-style track headers, timeline, right-docked Browser Panel, and lower detail editor with Piano Roll and Device Chain modes.
- Large harmonic header that shows the current project name, active key/scale, tempo, and meter at the playhead.
- Resizable lower detail editor area with a visible Piano Roll / Device Chain switch.
- Status bar showing build/platform information, undo depth, user-facing messages, and a `Log` button for diagnostics.
- Overlay panels for Audio Settings, Plugin Browser, and Diagnostics.
- Right-docked Browser Panel with Plugins and Scales tabs.
- App-level tooltips are enabled for mixer, transport, browser, timeline, device-chain, and overlay controls.
- Major workspace surfaces and reusable controls expose accessibility titles/descriptions for screen-reader clients.
- Escape closes open overlay panels; when no overlay is open, Escape returns keyboard focus to the main workspace.

## Project Files And Persistence

- New Project, Open Project, Save Project, and Save Project As actions from the Project menu.
- `.tseq` project package save/load support.
- Versioned project serialization through `project.json`.
- Project packages round-trip tracks, clips, notes, custom scales, tempo map, time signature map, harmonic regions, chord regions, rhythm settings, plugin references, and clip harmonic metadata.
- Track plugin state files are captured into project packages and restored when projects reopen.
- Missing plugin restore paths surface warnings without preventing project load.
- Browser Panel project-file listing safely enumerates VST3 bundles, audio files, and MIDI files from the currently open `.tseq` package, plus referenced audio assets from the current project.
- Window title updates to the current project package name after save/open.
- App settings persist separately from project data, including audio output settings and plugin registry cache.

## Timeline And Arrangement

- Unlimited-by-design MIDI tracks in the core project model.
- `+ Track` timeline control for adding MIDI tracks.
- Empty timeline/track-list space context menus can insert MIDI, Audio, and Return tracks.
- Dragging a Browser Panel instrument plugin into empty track space creates a MIDI track with that instrument.
- Dragging a Browser Panel audio-effect plugin into empty track space creates an Audio track with that effect in its device chain.
- Dragging a Browser Panel audio file into empty track space creates an Audio track with an audio clip referencing that file.
- Dragging a supported audio file from the OS into empty timeline space creates an Audio track with an audio clip.
- Dragging a Browser Panel MIDI file into empty track space creates a MIDI track with an imported MIDI clip.
- Track headers show track names, assigned instrument/device summaries, and ARM chips for MIDI recording.
- Track headers are vertically aligned with timeline lanes.
- MIDI clips can be created by double-clicking a track lane.
- MIDI and audio clips can be selected, dragged horizontally, resized from the right edge, copied, pasted, and deleted.
- MIDI clips open in the piano roll; selecting an audio clip keeps the timeline selection without opening MIDI editors.
- Audio clips render waveform overviews in the timeline from a background JUCE thumbnail cache.
- Unsupported audio-file imports report a warning and create no partial track.
- Unsupported or corrupt MIDI-file imports report a warning and create no partial track.
- Same-track clip overlap is prevented by the core model and command layer.
- Clip moves/resizes are command-backed and support undo/redo.
- Timeline marquee selection supports selecting clips and musical-structure regions.
- Context-aware Cmd/Ctrl+A selects all in the active field.
- Cmd/Ctrl+C and Cmd/Ctrl+V work for timeline clips, musical-structure regions, and piano-roll notes.
- Timeline paste uses the playhead when available and snaps pasted content to the grid.
- Continuous timeline playhead dragging from the ruler.
- Space toggles playback.
- Shift+Tab toggles the lower detail editor between Piano Roll and Device Chain.
- Cmd/Ctrl+Z, Cmd/Ctrl+Shift+Z, and Cmd/Ctrl+Y support undo/redo.
- The timeline defaults to 58 bars.
- If a dragged or resized clip extends past the visible timeline end, the timeline reveals additional bars in 2-bar increments.
- Mouse-wheel timeline zoom adjusts the visible timeline span.
- Ableton-style contextual grid lines and numbering adapt to zoom, including beat labels at usable densities and stronger labeled bar lines.
- Empty arrangement and track-list states describe the available actions for adding tracks or dropping plugins/audio/MIDI files.

## MIDI Clip Behavior

- MIDI clips store arrangement start, total length, source length, loop state, notes, and clip-local harmonic metadata.
- Clips can be loop-enabled or loop-disabled.
- Loop-enabled clips keep one source note list and repeat source content across the stretched arrangement duration.
- Editing source clip notes updates loop repetitions rather than duplicating note data.
- Piano-roll toolbar includes clip-specific length fields in bars:beats.
- Piano-roll toolbar includes a clip Loop toggle that controls whether stretching a MIDI clip loops source content or simply extends clip duration.
- Clip harmonic metadata can snapshot the project harmonic context across the clip duration.
- Moving clips into a different harmonic context does not destructively transpose MIDI pitches; out-of-context notes are evaluated and shown as accidentals.

## Audio Clip Behavior

- Audio tracks can own audio clips with source file reference, arrangement start, length, source offset, loop flag, stretch-to-tempo flag, and clip gain.
- Audio clip add, move, resize, delete, copy, and paste operations use the command stack and support undo/redo.
- Audio clip metadata is serialized in project files and missing audio sources surface project-package load warnings.
- Project JSON includes waveform-cache metadata entries for unique audio sources while keeping imported audio policy reference-only.
- Audio clips are materialized as Tracktion wave clips during playback sync and play through track mixer, routing, sends, returns, master, and meters.

## Musical Structure Lanes

- Song-level musical structure model for tempo, time signature, key center, scale/mode, and chord progression.
- Tempo map supports tempo nodes and tick/second conversion, including linear BPM interpolation in the core model.
- Time signature map supports meter markers and bar/beat conversion across meter changes.
- Timeline UI supports adding tempo nodes and time signature markers.
- Key center regions can be added, edited, dragged/resized, copied/pasted, and deleted.
- Scale/mode regions can be added, edited, dragged/resized, copied/pasted, and deleted.
- Chord progression lane displays chord regions.
- Chord regions can be generated from selected piano-roll notes via `Globalize Chords`.
- Chord regions can be dragged/resized, copied/pasted, and deleted.
- Borrowed chord regions can offer compatible same-key mode suggestions through a context menu.
- Structure-region operations are command-backed and undoable.

## Browser Panel, Scales, And Custom Scales

- Right-docked Browser Panel includes a Scales tab that preserves the former scale palette behavior.
- Browser Panel empty states distinguish no plugins, no project files, no project package, and no search matches.
- Scales tab shows built-in scale definitions grouped by category.
- Built-in library includes Chromatic, Major, Natural Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic Minor, Melodic Minor, Major Pentatonic, Minor Pentatonic, Major Blues, Minor Blues, Whole Tone, Diminished Half-Whole, and Diminished Whole-Half.
- Scale metadata supports names, categories, tags, aliases, descriptions, and search.
- Scales can be dragged from the palette onto the timeline scale/mode lane.
- Dropping a scale onto an existing scale region replaces it; dropping onto empty lane space creates a region.
- Custom Scale editor allows naming a scale and choosing pitch classes from a C-based 12-note keyboard.
- Custom scales validate for usable pitch selections, duplicate names, and metadata requirements.
- Custom scales are added to the project library and participate in lookup, serialization, scale lanes, piano-roll lanes, and recording transforms.

## Piano Roll Editing

- MIDI clips open in a lower piano-roll detail editor.
- Piano roll displays a time grid, visible note lanes, note-name header, note blocks, and playback/paste playhead.
- Notes can be created by double-clicking the grid.
- Single-clicking in the piano roll places the playhead at the selected beat for playback/paste targeting.
- Notes can be selected, marquee-selected, shift-added to an existing marquee selection, moved, resized from note edges, copied, pasted, duplicated, and deleted.
- Cmd/Ctrl+A selects all notes in the open piano roll when it has focus.
- Piano-roll copy/paste uses the app clipboard path and pastes relative to the piano-roll playhead when possible.
- Note dragging previews update note position and note name in realtime before drop.
- Left/right arrow keys move selected notes horizontally by the current grid.
- Up/down arrow keys move selected notes through the currently visible note lanes.
- Shift+Left/Right resizes selected note ends by the current grid.
- Shift+Up/Down inverts selected chord-like groups; for single-note melodies it octave-shifts the selected notes.
- Cmd/Ctrl+Up stacks a diatonic third above selected note timing groups.
- Cmd/Ctrl+Down removes the highest chord tone from selected note timing groups.
- Alt/Option+Left/Right steps arpeggio subdivision shorter or longer.
- Alt/Option+Up/Down cycles arpeggio pattern.
- Arpeggiation destructively rewrites selected notes as ordinary MIDI notes so MIDI export preserves the result.
- Supported arpeggio patterns include Up, Down, Up-Down, Down-Up, Outside-In, Inside-Out, and Random.
- Shift+S performs scalar fill between two selected notes in different visible lanes.
- Scalar fill inserts intervening scale notes at the current rhythmic grid.
- Scalar fill can shift the end note forward or backward so the final run is contiguous and fits the available grid spacing.
- Piano roll supports horizontal and vertical zoom with modifier mouse-wheel gestures.

## Scale-Aware Piano Roll Lanes

- Piano-roll lanes are generated from the active harmonic context.
- Default view hides non-scale chromatic lanes.
- `Show Chromatic` toggle reveals all 12 chromatic pitch classes.
- `C` hotkey toggles chromatic reveal while the piano roll has focus.
- Non-scale chromatic lanes are greyed when chromatic reveal is active.
- Used accidental lanes remain visible per clip after chromatic reveal is disabled.
- Accidentals are clip-specific, not track-global.
- If harmonic regions move or are deleted, notes that no longer belong to the active scale get their own accidental lanes rather than being folded into a native lane with the same letter name.
- Note labels and lane spellings use context-aware enharmonic spelling.
- A clip spanning multiple key/scale regions is split into harmonic piano-roll segments.
- Secondary note-name headers appear at key-center or scale/mode changes inside a clip.
- Secondary harmonic headers align equivalent letter-name rows across regions where possible, like key-signature changes in notation.
- Secondary headers are narrower than the main note-name header and show a hover tooltip explaining that note lanes update at that point.
- Piano roll can show chord-tone/non-chord-tone overlays for chord regions intersecting the open clip.

## Transposition And Theory Tools

- Chromatic Transpose button maps clip notes into the current harmonic context while preserving semitone relationships.
- Scale-Degree Transpose button maps notes by scale degree into the current harmonic context.
- If notes are selected, scale-degree transpose operates on the selected notes rather than always transposing the whole clip.
- Per-note harmonic interpretation metadata stores the harmonic lens a note was authored or copied from.
- Selected scale-degree transpose uses each selected note's harmonic interpretation and project position, so different segments of the same clip can transpose against different song harmonic regions.
- Note interpretation helpers preserve scale degree, alteration, melodic contour, octave, and spelling where possible.
- Chord stacking and chord inversion tools are undoable core commands.
- Chord recognizer supports common chord qualities and formats chord names without Roman numerals.
- Borrowed chord analysis detects chord regions that are outside the active scale and can suggest compatible mode regions.

## Rhythm, Grid, And Tuplets

- Core time model uses strong tick-domain types, musical bar/beat positions, tempo maps, and time signature maps.
- Project rhythm settings store the active piano-roll grid/shortest note value.
- Piano-roll toolbar includes a grid selector.
- Grid divisions affect piano-roll grid drawing, note entry, snapping, note movement, resizing, quantization, and arpeggiation.
- Tuplet menu can enable or disable triplets, quintuplets, septuplets, and nonuplets.
- Disabled tuplets are hidden from the active grid choices.
- Rhythm settings are serialized and undoable.

## MIDI Recording And Input

- JUCE-backed MIDI input service lists available MIDI input devices.
- Transport bar MIDI input selector opens the selected input device.
- Track ARM chips choose the destination track for MIDI recording.
- Record toggle captures incoming MIDI notes.
- If no clip is selected for recording, recording can create a default recording clip on the armed track.
- Recording can extend a selected or active recording clip as needed.
- Input quantization toggle snaps recorded events to the current piano-roll grid.
- Scale Lock modes are Off, Nearest, Up, and Down.
- With Scale Lock off, recorded MIDI keeps the performed pitches, including accidentals.
- With Scale Lock enabled, incoming pitches are mapped into the active harmonic context according to the selected mode.
- MIDI input callback uses a fixed FIFO-style queue and defers project mutation, logging, quantization, and theory work to the message/control path.

## Playback And Transport

- Tracktion-backed playback engine initializes an audio device and a Tracktion edit.
- Project playback sync converts core project tracks, clips, tempo map, time signatures, notes, and assigned instruments into a Tracktion playback graph before playback.
- Start and Stop controls operate project playback.
- Return to Start seeks to project start.
- Playback Loop toggle loops the prepared project playback range.
- Playback playhead is polled from the UI/control path and reflected in the timeline and piano roll.
- Stop/rebuild paths aim to avoid stuck notes and stale transport state.
- Debug assertions document that playback engine control methods are message/control-thread APIs.

## Audio Settings

- Audio Settings overlay lists available output devices.
- The panel shows current output, sample rate, buffer size, and status message.
- Output device can be changed and applied.
- Audio output choice and/or JUCE device state are persisted in app settings.
- Startup attempts to restore saved audio settings and reports useful warnings if restore fails.

## Plugin Hosting

- Browser Panel Plugins tab lists cached plugins, supports search, filters for instruments/audio effects/project files, and shows scan status.
- Browser Panel plugin rows can be dragged as typed plugin payloads.
- Browser Panel project-file rows can be dragged as typed project-file payloads and show file-type labels plus size or missing-reference metadata.
- Dropping a plugin from the Browser Panel onto a track header assigns instruments or appends audio effects according to the plugin metadata.
- Dropping a plugin from the Browser Panel into empty track space creates a configured track transactionally.
- Lower Device Chain view displays the selected track's left-to-right signal flow and accepts compatible Browser Panel plugin drops.
- Device Chain empty states distinguish no selected track from an empty chain that can accept a compatible plugin drop.
- Device Chain faceplates show enable/bypass state, plugin name, plugin kind, channel/state summary when available, and an `Edit` button for supported full plugin GUIs.
- Device Chain bypass edits are command-backed and push a playback graph sync so audio follows the project model.
- Browser Panel plugins can be dropped into Device Chain gaps to insert at a specific point.
- Browser Panel plugins can be dropped onto Device Chain faceplates to replace/hot-swap an existing command-backed device.
- Device Chain faceplates can be dragged left/right to reorder devices where the resulting chain is valid.
- Device Chain faceplates include an `X` remove affordance for command-backed devices.
- Device Chain append, insert, replace, remove, reorder, and bypass operations are undoable.
- Floating project plugin editor windows are opened through the playback-engine abstraction and are closed before project plugin instances are destroyed during full sync/shutdown.
- Existing Plugin Browser overlay remains available for scan/assign/test-phrase/editor workflows.
- VST3 plugin scanning service with default macOS user/system VST3 scan paths.
- Plugin scan runs on a worker thread, not the audio thread.
- Plugin registry cache persists scanned VST3 metadata.
- Scanner dead-man's-pedal file records crash/failure context for scanning.
- Plugin Browser can scan VST3s, refresh cached results, show search paths/status, and list cached plugin name, manufacturer, format, instrument flag, and identifier/path.
- Plugin Browser can assign a selected VST3 instrument to a selected MIDI track.
- Assigned instrument references are stored on tracks and serialized in projects.
- Plugin Browser can play/stop a hardcoded test phrase through a selected instrument.
- Plugin Browser can open the loaded plugin editor when supported.
- Tracktion playback sync loads assigned track instruments and restores plugin state files when available.
- Live plugin parameter state is observed and guarded around high-risk project sync, transport, and piano-roll edit events.
- Focused debug support tracks Synthesizer VST3 controls including Osc1 Mod Amount, Osc1 Wavefold Amount, and Osc1 Carrier Wave.

## Mixer And Routing

- Track headers provide per-track volume sliders, precise dB text fields, pan sliders with center indication, activator On/Off toggles, Solo toggles, and ARM chips.
- Track-header volume text accepts numeric dB values and `-inf`/`off`, using the shared mixer dB parser and clamp rules.
- Mixer edits are command-backed and participate in undo/redo.
- Track-header meters poll the real playback-engine meter snapshot infrastructure.
- Track routing dropdowns expose valid Audio From, Audio To, MIDI From, and MIDI To choices for the current track type.
- Routing edits are command-backed and validated through the core routing model so missing and cyclic routes are rejected.
- Multiple tracks can be soloed at once.
- Return tracks can be inserted from the timeline or track-header context menus and can host audio-effect device chains.
- A singleton master track can be inserted from the timeline or track-header context menus and can host mastering effects.
- Track headers expose compact send sliders for valid return-track destinations; send edits are command-backed and persist with project routing.
- Send routes participate in routing validation, so feedback cycles through return tracks are rejected before playback sync.
- Return tracks fed by a soloed source are treated as part of that source's audible solo path during playback graph sync.
- Timeline tracks can expose automation lanes beneath the clip row for track volume, pan, return-send levels, and scanned plugin parameters.
- Automation points can be created by clicking a visible lane, dragged with timeline snapping, deleted with Delete/Backspace, and edited through command-backed undo/redo.
- Automation lanes serialize with the project and drive Tracktion playback state for volume, pan, sends, and matched plugin parameters.
- Automation playback is applied from the engine control/message path on sync/start/seek and a 30 Hz playback timer; Tracktion owns the resulting audio graph application and smoothing.
- Device removal and replacement invalidate automation deterministically: removing a device removes that slot's parameter and bypass lanes with undo restoration, while replacing a device removes stale parameter lanes and preserves slot-bypass automation.

## MIDI Import And Export

- Browser Panel and timeline drop paths can import Standard MIDI Files into a new MIDI track and clip.
- MIDI import supports PPQ-based format-0 and format-1 files, note-on velocity zero as note-off, running status, and timing conversion into the app's 960 PPQ tick grid.
- MIDI import merges imported MIDI tracks into one clip while preserving musical note timing; it does not rewrite the song's global tempo map.
- Project menu includes `Export Open Clip as MIDI...`.
- Export writes a standard MIDI file for the currently open piano-roll clip.
- Export uses the clip's start tempo and time signature for file options.
- Export path defaults to an `exports` folder inside the current `.tseq` package when available, otherwise Documents.
- Export failures return user-visible messages.
- Core MIDI importer/exporter writes and reads Standard MIDI Files and is covered by tests.

## Diagnostics And Error Handling

- Core logger stores formatted diagnostic entries.
- Each app session writes a fresh diagnostics log to `~/Library/Application Support/TheorySequencer/diagnostics.log`.
- Diagnostics overlay shows log entries with Refresh and Close controls.
- App services expose user-facing warnings/errors that appear in the status bar.
- Plugin state traces are logged around piano-roll mouse down/double-click, note creation, edit commands, playback dirty marking, deferred plugin restore scheduling, project sync, return-to-zero, and playback start.
- Non-throwing result types are used for command and MIDI export error paths.
- Project, plugin, scan, audio, and export failures surface clear messages instead of silently failing.

## Undoable Command System

- Core command framework supports execute, undo, redo, failure messages, and undo/redo stacks.
- Command-backed mutations include adding tracks, adding/moving/resizing/deleting clips, assigning instruments, adding/deleting/resizing/moving notes, adding/replacing/deleting key regions, scale regions, chord regions, tempo nodes, time signature markers, rhythm settings, custom scales, chord stacking, chord inversion, arpeggiation, transposition, and globalizing chord progressions.
- Successful project commands mark playback sync dirty so playback is prepared from current project state.

## Test And QA Infrastructure

- Catch2 core test suite covers time model, pitch model, scale library, custom scale validation, harmonic context resolution, project model, command stack, serialization, MIDI import/export, accidental visibility, chord recognition, chord progression/globalize behavior, harmonic overlay, borrowed chord analysis, recording input transforms, automation playback snapshots, automation target invalidation, and diagnostics logger behavior.
- App-enabled integration tests cover typed/drag-created track insertion, imported audio/MIDI track creation, imported-media package round trips, plugin-state package placeholders, plugin registry behavior, and live metering paths.
- App-enabled integration test target covers Synthesizer VST3 state persistence when the plugin is installed.
- macOS native editor driver and external mouse event helper support difficult plugin-editor interaction tests.
- Synthesizer regression harness can assign the real plugin, open its editor, edit watched controls, create clips/notes through piano-roll-like interactions, sync playback, return to zero, start/stop playback, challenge defaults, and verify parameter values numerically.
- Manual QA checklist documents release-readiness coverage for launch, plugin scan, track creation, device chains, plugin GUI state, clips, piano roll, mixer playback, returns/sends/master, automation, audio/MIDI import, package recovery, keyboard/UI smoke, and MIDI export.
- Audio thread audit documents realtime boundaries, current safety status, and remaining risks.

## Current Limitations And Deferred Items

- Audio recording, audio clip editing, vertical/session mixer view, audio render/export, AU/VST2/CLAP/LV2/AAX hosting, sample-accurate automation editing, automation copy/paste/marquee editing, staff notation, Roman numeral analysis, and full melodic-function analysis are not implemented.
- Sidechain-ready routing references are modeled, serialized, and validated, but true sidechain audio routing is not engine-mapped yet; the Tracktion adapter warns and falls back for sidechain endpoints.
- Manual chord-name entry/parsing in the chord progression lane is deferred.
- Installer packaging, code signing, and notarization are not implemented.
- Windows and Linux build docs exist, but local verification has focused on macOS/headless test paths.
- Known runtime and packaging issues are tracked in `docs/KNOWN_ISSUES.md`.
