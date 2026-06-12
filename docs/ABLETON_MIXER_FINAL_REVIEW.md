# Ableton Mixer Final Integration Review

Last updated: 2026-06-10

This review closes `docs/agent/TheorySequencer_Ableton_Mixer_Prompt_Pack.md` Prompt 18. It checks the shipped Ableton-style mixer work against the prompt requirements using code review, existing docs, and automated tests that do not run the expensive Synthesizer parameter-wipe stress path.

## Requirement Status

| # | Requirement | Status | Evidence | Remaining Risk |
|---|-------------|--------|----------|----------------|
| 1 | Right-docked tabbed Browser Panel with Plugins & Project Files and Scales tabs. | Shipped | `MainComponent` owns `BrowserPanelComponent`; `BrowserPanelComponent` has Plugins and Scales tab buttons, plugin rows, project-file rows, filters, and scan status. | Hands-on drag/filter QA still recommended. |
| 2 | Existing Scale Palette functionality preserved inside Scales tab. | Shipped | `BrowserPanelComponent` embeds `ScalePaletteComponent`; custom scales and drag-to-scale-lane code remain in `ScalePaletteComponent` and timeline scale drop paths. | Manual scale drag regression pass still recommended. |
| 3 | Drag-and-drop instruments to timeline/track headers and empty track space. | Shipped | Browser payloads are accepted by `TimelineComponent`, `TrackListComponent`, `DeviceChainComponent`, and `AppServices::createTrackFromPlugin*` / device-chain helpers. | UI drag automation is not present; covered by code review and focused service tests. |
| 4 | Horizontal mixer track headers with volume, dB text input, pan, mute/activator, solo, meters, and routing menus. | Shipped | `TrackListComponent` implements mixer rows; `MixerStrip`, `Routing`, mixer commands, and meter snapshots back the controls. | Full GUI value-entry QA remains manual. |
| 5 | Context menus for Insert MIDI Track, Insert Audio Track, Insert Return Track. | Shipped | `TimelineComponent` and `TrackListComponent` context menus call `AppServices::insertTrack` for MIDI, audio, return, and master tracks. | Manual context-menu click pass still recommended. |
| 6 | Lower-pane Piano Roll / Device Chain toggle, including Shift+Tab. | Shipped | `DetailEditorComponent` hosts Piano Roll and Device Chain modes; `MainComponent` routes Shift+Tab to the lower editor toggle. | Manual focus/shortcut pass still recommended. |
| 7 | Device chain UI with serial signal flow, bypass, plugin name, floating editor button, hot-swap/append/replace workflows. | Shipped | `DeviceChainComponent`, `MixerCommands`, and `AppServices` implement append/insert/replace/remove/reorder/bypass/editor paths. | Device drag/drop and editor behavior need hands-on QA across third-party plugins. |
| 8 | VST3 audio effect scanning, hosting, state persistence, and playback. | Shipped with plugin-dependency risk | `PluginScanService` and `PluginRegistry` classify instruments/effects and parameter metadata; `TracktionPlaybackEngine` materializes device chains and per-slot state files. | Third-party plugin compatibility remains vendor/JUCE/Tracktion dependent. |
| 9 | Automation lanes for volume, pan, and exposed VST3 parameters, driving real audio/plugin parameters. | Shipped with timing limit | `Automation`, `AutomationPlayback`, timeline lane editing, serialization, and Tracktion adapter binding cover volume, pan, sends, bypass, and matched plugin parameters. | Automation is Tracktion-managed/control-rate, not sample-accurate custom DSP automation. |
| 10 | Return tracks, sends, bussing/sidechain-ready routing, and master track with device chain. | Shipped with sidechain limit | Core routing, sends, return/master track types, validation, serialization, UI controls, and Tracktion aux/master graph mapping are present. Sidechain endpoints are modelled and serialized. | True sidechain graph mapping is not implemented; engine sync warns and falls back for sidechain endpoints. |
| 11 | Audio tracks with waveform rendering and playback. | Shipped | `AudioClip`, timeline waveform thumbnail rendering, audio import/drop paths, package warnings, and Tracktion wave-clip materialization are present. | Large-file waveform responsiveness still needs broader manual stress QA. |
| 12 | Project save/load/migration for all new state. | Shipped | Schema v2 serializer includes tracks, mixer/routing/device chains/audio clips/automation/audio assets; v1 migration maps legacy plugin references into device chains. | Project-package move/recovery scenarios need manual QA. |
| 13 | Audio quality and realtime safety: no avoidable clicks, no fake meters, no audio-thread blocking, no state races. | Shipped for current architecture with documented risks | `docs/AUDIO_THREAD_AUDIT.md` records control-path sync, Tracktion-owned render path, real LevelMeterPlugin-backed meters, plugin scan/file IO boundaries, and Prompt 16 UI churn reductions. | Tracktion internals and third-party plugins are outside direct control; `syncProject()` remains synchronous. |
| 14 | Existing DAW/theory features remain intact. | Shipped with regression coverage | Feature inventory, core tests, import/export tests, piano-roll/theory docs, and manual QA checklist cover the preserved DAW/theory workflows. | Full interactive UI regression pass has not been executed in this environment. |

## Automated Verification

Prompt 18 verification executed and passed these focused checks:

- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_app`

The full integration executable also contains the real Synthesizer VST3 state regression harness. That harness is intentionally not part of this prompt's default verification because the current prompt-pack continuation was asked to avoid the extensive parameter-wipe stress loop.

App-enabled integration startup still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`; the selected non-VST tests pass after that assertion is logged.

## Manual QA Status

`docs/MANUAL_QA_CHECKLIST.md` is the required hands-on checklist for release readiness. This environment did not execute the full manual QA pass, UI drag/drop pass, third-party plugin compatibility matrix, or package-move recovery pass.

## Remaining Risks

- App/integration startup still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`; selected non-VST tests continue to pass.
- Sidechain-ready route endpoints are modelled, serialized, and warned about, but true sidechain processing is not engine-mapped yet.
- Automation is useful for playback binding but is currently control-rate/timer-driven rather than sample-accurate.
- Large sessions may make synchronous Tracktion edit preparation slow.
- Third-party VST3 behavior and realtime safety remain partly outside this codebase's direct control.
- Interactive GUI actions such as context menus, drag/drop, floating editors, and narrow-window layout need the manual checklist pass before a release candidate.
