# Audio Thread Audit

Prompt 16 performance/realtime hardening audit update: 2026-06-10.

## Source Rules

The persistent `docs/AUDIO_THREAD_RULES.md` file is not present in this checkout. The applicable rules were read from `docs/agent/02_ARCHITECTURE_GUARDRAILS.txt`: audio/realtime paths must not allocate, lock, read or write files, parse JSON/XML, scan plugins, call UI code, perform expensive theory analysis, block on futures, log heavily, or traverse large project models dynamically.

## Realtime Entry Points

- Tracktion Engine owns the audio render callback through its device manager and edit/transport graph. TheorySequencer does not currently implement `AudioIODeviceCallback`, `processBlock`, `prepareToPlay`, or `getNextAudioBlock`.
- `TracktionPlaybackEngine::startPlayback()` and `TracktionPlaybackEngine::playTestPhrase()` only start Tracktion transport from the control/message path.
- `MidiInputRecordingService::handleIncomingMidiMessage()` is a JUCE MIDI device callback. It is not the audio render callback, but it is treated as realtime-adjacent and kept lock-free/allocation-free.

## Prepared Playback Boundary

The current playback preparation boundary is `TracktionPlaybackEngine::syncProject()`.

That method runs before playback from `AppServices::startProjectPlayback()` when the project is dirty. It stops transport, rebuilds the Tracktion edit when graph topology requires it, applies tempo and time signature data, maps TheorySequencer tracks to Tracktion audio tracks, loads hosted VST3 device-chain slots, restores plugin state on the control/message path, configures volume/pan/mute/solo/routing/sends/returns/master processing, creates Tracktion MIDI clips, and materializes notes into each Tracktion sequence.

`TracktionPlaybackEngine::createProjectMidiClip()` is the final MIDI materialization step. It traverses TheorySequencer clips and notes on the control path, not from the audio callback. The audio callback consumes the prepared Tracktion edit.

The prompt's precomputed MIDI playback-buffer requirement is therefore satisfied for MVP by Tracktion sequence materialization: the core project model is converted into backend MIDI clips ahead of render time.

`TracktionPlaybackEngine::createProjectAudioClip()` likewise materializes core audio clip references into Tracktion wave clips before render time. Missing or invalid audio files are surfaced as sync warnings by the control path where possible; file IO is not performed by TheorySequencer from a custom audio callback.

Automation playback uses the same prepared boundary. `syncProject()` stores an app-owned core project snapshot for automation lookup, and playback/start/seek/control-timer callbacks query that snapshot from the message/control path before writing Tracktion volume, pan, aux-send, plugin-parameter, and bypass state. The render callback does not traverse the core project or automation lane data directly. As of Prompt 16, the automation playback timer is only armed when the prepared project actually contains automation lanes.

## Current Safety Status

- No custom audio render callback exists in `src/`.
- Playback engine APIs are now explicitly documented as control-thread APIs.
- Tracktion control methods now assert the JUCE message thread when a `MessageManager` exists.
- Plugin scanning runs in `PluginScanService::scanVst3()` on a worker thread started by `startVst3Scan()`, not from playback/render code.
- Plugin state file reads happen during hosted device-slot load in `syncProject()`.
- Live hosted plugin state capture/restore happens around Tracktion edit rebuilds, transport positioning, and playback start on the control/message path. Captured state is keyed by track/device slot, so instrument and effect chains do not overwrite each other.
- Plugin state capture and writes happen during project save after `AppServices::saveProjectAs()` stops playback. Device-chain state files are written under per-slot package paths.
- Return sends/returns and master-chain plugins are created as Tracktion plugins during sync. Send level, mute, and aux bus selection are configured before playback rather than from the realtime callback.
- Track volume, pan, mute, and solo are applied to Tracktion track/volume plugin state from the control path. Tracktion owns any internal smoothing or graph application after this point.
- Automation values for track volume, pan, send level, device bypass, and matched VST3 parameters are applied from the Tracktion adapter control/message path on sync, playback start, playhead moves, and a 30 Hz message-thread timer during playback only when the prepared project has automation lanes. This pass is Tracktion-managed/control-rate automation, not sample-accurate custom DSP automation.
- Automation target validation is resolved against the prepared project snapshot. Missing tracks, missing return targets, and removed device slots are skipped before engine binding.
- Per-track and master meter taps use Tracktion's built-in `LevelMeterPlugin` instances in the prepared edit graph. TheorySequencer attaches/removes `LevelMeasurer::Client` objects only from the message/control path after sync and before edit teardown.
- `PlaybackEngine::getMeterSnapshot()` is a UI/control-path polling API. It reads and clears Tracktion meter-client peaks into plain app-owned snapshot structs, reports stopped transport as floor/zero, and performs no plugin scanning, file IO, logging, or project traversal.
- Core mixer math and `MeterBallistics` are JUCE/Tracktion-free helpers intended for UI/control code. They do not run in the audio callback.
- Project JSON/package load and save happen in `AppServices` and core serialization, not from the render path.
- Browser Panel package-file enumeration is UI/control-path work and is not tied to audio playback. Prompt 16 removed project-file enumeration from the plugin-scan status timer; plugin scan progress now refreshes cached plugin rows/status without repeatedly walking the project package.
- Main UI polling remains on the message thread. Prompt 16 removed an idle Browser repaint from the global 24 Hz UI timer and changed the lower detail editor to refresh only the visible Piano Roll or Device Chain subview.
- MIDI input callback work is limited to note filtering, primitive field copy into a fixed-size `juce::AbstractFifo`/`std::array`, and an atomic dropped-event counter.
- MIDI recording theory analysis, quantization, command execution, logging, and project mutation happen later in `AppServices::processMidiRecordingEvents()` from the message timer.

## Remaining Risks

- Third-party VST3 plugin realtime behavior is outside this codebase's direct control.
- Tracktion Engine internals own the render callback; this audit verifies our adapter boundary, not Tracktion internals.
- `syncProject()` is synchronous and rebuilds the edit on the message path. This is safe for audio-thread rules, but may become slow for large sessions, especially once projects have many VST3 effects, sends, return tracks, and master-chain devices.
- The current meter story relies on Tracktion's existing level-meter plugins being in the graph. This avoids fake meters and uses real rendered audio, but the actual audio-thread tap implementation is Tracktion-owned.
- RMS values are represented in the app snapshot type but currently marked unavailable because Tracktion's built-in meter plugin is configured as a peak meter. A later dedicated tap can publish peak and RMS simultaneously.
- Device bypass currently maps to Tracktion plugin enabled state from the control path, including automation updates. Future automation-driven bypass should add an explicit wet/dry or gain-smoothed transition where Tracktion's built-in behavior is insufficient.
- Current automation timing is control-rate and timer-driven. It is musically useful for lane editing and playback binding, but fast parameter moves may need a later block/sample-accurate Tracktion automation integration to avoid zippering on plugins that do not smooth internally.
- `getPlayheadPosition()` polls Tracktion transport from the UI timer. It must remain a UI/control-path operation.
- `Logger` allocates, stores strings, and writes to console. It is not thread-safe and must not be called from audio or MIDI device callbacks.
- MIDI input queue overflow drops events after 512 queued messages. The drop count is tracked, but the UI does not yet surface a warning.
- Future live-edit playback updates must not mutate Tracktion/core project state from the render callback.

## Planned Follow-Ups

- Add a user-visible MIDI dropped-event indicator in the transport/status area.
- Render visible meter components from `PlaybackEngine::getMeterSnapshot()` and core `MeterBallistics`, not from plugin state or UI timers with synthetic values.
- Consider a dedicated immutable `PreparedPlaybackProject` snapshot if live editing or large projects make synchronous Tracktion edit rebuilds too slow.
- Move project sync to a cancellable worker/control preparation step once the app needs live playback updates at scale.
- Keep plugin scan, plugin load, project package IO, and plugin state capture explicitly outside the audio callback.
