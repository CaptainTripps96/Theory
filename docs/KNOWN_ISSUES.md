# Known Issues

Last updated: Ableton Mixer Prompt 18 final integration review.

## MVP Verification Gaps

- The full 20-step MVP scenario has not been completed through automated UI interaction in this environment. Earlier AppleScript UI automation attempts were blocked by macOS assistive-access permissions, so this pass used code review, pure regression coverage, build/test, and launch smoke verification.
- A focused Synthesizer VST3 integration regression now assigns a real instrument, opens its editor, edits the three hosted Oscillator 1 controls from the manual repro, emulates piano-roll single-click and double-click note entry, syncs playback, returns to zero, starts/stops playback, and verifies the parameters survive. A human should still run the broader plugin playback checklist for other vendors and full UI workflows.
- Debug app sessions write diagnostics to `~/Library/Application Support/TheorySequencer/diagnostics.log`. Focused plugin-state traces around piano-roll edits, sync, return-to-zero, and playback start are now opt-in with `TSQ_PLUGIN_STATE_TRACE=1` so ordinary editing is not slowed by verbose trace logging.
- The new `Export Open Clip as MIDI...` menu action is compile- and launch-smoked, and the core export path is covered by tests. The native file chooser flow should still be clicked manually during the next interactive QA pass.

## Runtime Issues

- App launch still prints a JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during plugin-format setup. The app continues initialization afterward, but the assertion should be investigated before release.
- Third-party plugin loading remains dependent on JUCE/Tracktion/plugin-vendor behavior. The app now surfaces load failures, but individual plugin compatibility still needs manual testing.
- Sidechain-ready route endpoints are modeled, serialized, and validated, but the Tracktion adapter does not yet map true sidechain audio routing. Engine sync currently warns and falls back for sidechain endpoints.

## Build And Packaging Issues

- App builds on macOS still emit third-party Tracktion/JUCE warnings and no-symbol `ranlib` notices.
- App relinking still emits the duplicate `tsq_core` static-library linker warning.
- The GitHub Actions workflow has been added for headless tests, but it has not been observed running remotely.
- Windows and Linux build instructions are documented but not verified locally.
- Installer packaging, code signing, and notarization are intentionally not implemented yet.

## Documentation Issues

- The persistent docs named by the original prompt pack are still incomplete: `docs/AI_CONTEXT.md`, `docs/PRODUCT_SPEC.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/CODING_STANDARDS.md`, and `docs/AUDIO_THREAD_RULES.md` do not exist yet. The active source of truth remains `docs/agent/*` plus the newer focused docs.
