# Manual QA Checklist

Use this checklist for release-readiness passes. Record the app build, macOS version, CPU architecture, audio device, sample rate, buffer size, package path, and VST3 instrument/effects tested.

## Launch And Baseline

- [ ] App launches without crashing.
- [ ] New project can be created.
- [ ] Diagnostics log opens from the status bar `Log` button.
- [ ] Audio engine initializes or reports a useful user-visible error.
- [ ] Existing app settings are restored or a useful fallback is used.
- [ ] Timeline defaults to 58 measures and reveals two more measures when a dragged clip extends past the current end.
- [ ] Main timeline beat/bar numbering remains readable at narrow and wide zoom levels.

## Browser And Plugin Scan

- [ ] Browser Panel opens and remains docked on the right.
- [ ] Plugins and Project Files tab shows the expected sections.
- [ ] Scales tab preserves the old scale-palette behavior.
- [ ] VST3 scan starts.
- [ ] Scan progress/status is visible.
- [ ] Scan completes without crashing the app.
- [ ] Instruments and effects are categorized correctly.
- [ ] Failed or skipped plugins are visible in diagnostics/status when applicable.
- [ ] Plugin registry persists after restart.
- [ ] Search/filter updates plugin rows without disturbing project-file or scale rows.

## Track Creation

- [ ] MIDI track can be created from the timeline/context menu.
- [ ] Audio track can be created from the timeline/context menu.
- [ ] Instrument drag from Browser to empty timeline space creates a MIDI track.
- [ ] Effect drag from Browser to empty timeline space creates an audio track.
- [ ] Dragging an instrument to an existing MIDI track assigns it.
- [ ] Dragging an effect to an existing track appends it to the device chain.
- [ ] Unsupported file/plugin drops fail with a clear status message.
- [ ] Undo/redo works for context-menu track creation and drag-created tracks.

## Device Chain And Plugin GUI

- [ ] Device Chain view follows the selected track.
- [ ] Instrument and audio effects can be appended, replaced, reordered, bypassed, and removed.
- [ ] Device-chain undo/redo restores the expected slots and bypass states.
- [ ] Floating plugin GUI opens when supported.
- [ ] Plugin GUI closes cleanly.
- [ ] Edit a plugin parameter, save/reload the project, and verify the parameter state is preserved.
- [ ] For Synthesizer, edit Oscillator 1 Modulator Amount, Wavefolder Amount, and Wave Shape; after creating a MIDI clip, adding a note in the piano roll, returning to zero, and pressing play, the edited sound remains intact.
- [ ] If plugin settings reset, inspect `~/Library/Application Support/TheorySequencer/diagnostics.log` and find the first event where the watched parameter values returned to defaults.

## Clips And Piano Roll

- [ ] MIDI clip can be created, selected, moved, resized, copied, pasted, and deleted.
- [ ] Clip length fields update clip length in bars and beats.
- [ ] Loop toggle changes timeline-stretch behavior between looping content and extending clip duration.
- [ ] Single-clicking in the piano roll places the piano-roll paste/playhead cursor at the selected beat.
- [ ] Copy, paste, and `Command+A` work in app text fields and piano-roll workflows without triggering the system alert sound.
- [ ] Notes can be added, selected, moved, resized, copied, pasted, and deleted.
- [ ] Selected notes move left/right with arrow keys and snap to the current grid.
- [ ] Selected notes move up/down through visible note lanes with arrow keys.
- [ ] Dragging notes vertically updates the note-name label in realtime.
- [ ] Holding Shift while marquee-selecting adds notes to the current selection.
- [ ] Scalar fill works for ascending and descending runs.
- [ ] Scalar fill shifts the end note later when intermediate scale tones need room.
- [ ] Scalar fill shifts the end note earlier when there is too much horizontal space.
- [ ] Scale-aware headers appear at key/scale changes inside a clip.
- [ ] Accidentals remain visible per clip after harmonic-region changes.
- [ ] Notes maintain correct harmonic interpretation after copying across regions and using scale-degree transpose.

## Mixer Playback

- [ ] Project playback starts.
- [ ] Playhead moves in timeline and piano roll.
- [ ] MIDI clips play through the assigned VST3 instrument.
- [ ] Audio clips play through the track device chain.
- [ ] Track volume affects audible level and meters.
- [ ] Track pan affects stereo position.
- [ ] Mute silences the track.
- [ ] Solo behavior isolates the expected tracks.
- [ ] Meters move during playback and fall to silence after stop.
- [ ] Stop works.
- [ ] Returning to start works.
- [ ] No stuck notes remain after stop.
- [ ] Undo/redo works for mixer volume, pan, mute, solo, routing, and send edits.

## Returns, Sends, And Master

- [ ] Return track can be created.
- [ ] Reverb or delay effect can be added to a return track.
- [ ] Audio or MIDI track can send signal to the return track.
- [ ] Send amount changes are audible.
- [ ] Solo behavior with return tracks is musically sensible and does not accidentally mute the return path.
- [ ] Master effect can be inserted.
- [ ] Master effect processes the final output.
- [ ] Save/reload preserves return tracks, send levels, routing, and master devices.

## Automation

- [ ] Volume automation can be drawn and heard.
- [ ] Pan automation can be drawn and heard.
- [ ] Send automation can be drawn and heard.
- [ ] Plugin-parameter automation can be drawn and heard.
- [ ] Automation curves save and reload.
- [ ] Deleting or replacing a device invalidates obsolete plugin-parameter automation deterministically.
- [ ] Automation playback has no obvious zipper noise or clicks during normal use.
- [ ] Undo/redo works for automation lane creation, point edits, and lane deletion.

## Audio And MIDI Import

- [ ] Audio file can be imported by Browser drag.
- [ ] Audio file can be imported by command/context flow.
- [ ] Imported audio clip displays a waveform.
- [ ] Imported audio clip can be moved and resized.
- [ ] Imported audio clip plays through track effects and master effects.
- [ ] Imported MIDI file can be dragged from Browser into the project.
- [ ] Imported MIDI notes appear at expected pitches and timings.
- [ ] Unsupported audio/MIDI files fail with clear status messages.
- [ ] Imported audio and MIDI tracks save/reload with expected clip metadata.

## Project Package And Recovery

- [ ] Project saves to a `.tseq` package.
- [ ] Project package contains `project.json`.
- [ ] Plugin-state files are written or useful warnings are shown.
- [ ] Saved project opens after app restart.
- [ ] Moving a project package preserves relative package contents where supported.
- [ ] Missing plugins produce visible warnings without preventing project load.
- [ ] Missing audio files produce visible warnings without preventing project load.
- [ ] Invalid automation targets are ignored or warned about without crashing.
- [ ] Route cycles are rejected or repaired with a useful warning.
- [ ] Timeline, clips, notes, harmonic regions, mixer state, device chains, sends, automation, instrument references, and plugin states round trip.

## Export MIDI

- [ ] MIDI export command is available where expected.
- [ ] Exported MIDI file is written to the chosen path.
- [ ] Export failure reports a useful message.
- [ ] Exported MIDI imports into another DAW or MIDI utility with expected note timing and pitches.

## Keyboard And UI Smoke

- [ ] Shift+Tab toggles the lower editor reliably.
- [ ] Escape closes open overlays or clears keyboard focus without destroying musical selections.
- [ ] Track header controls remain readable and clickable.
- [ ] Context menus open for tracks, clips, and timeline space.
- [ ] Browser scale drag still works.
- [ ] Custom scale creation/editing still works.
- [ ] Diagnostics overlay Refresh and Close controls work.
- [ ] Narrow-window layout avoids overlapping clip metadata and clip-length fields.

## Regression Sign-Off

- [ ] Run core tests with `ctest --preset tests --output-on-failure`.
- [ ] Run focused non-VST integration tests for track creation/import, package save/load, plugin registry, and meters.
- [ ] Run the Synthesizer VST state regression only when investigating plugin-state persistence or before a release candidate.
- [ ] Record any failed checklist item with exact steps, expected behavior, actual behavior, logs, and project package.
