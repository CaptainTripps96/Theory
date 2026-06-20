# Expression Mode Closeout

Date: 2026-06-16

## Summary

Expression Mode now has a first complete internal implementation pass across model, serialization, commands, UI, playback preparation, first-party synth playback, mixer route playback, MIDI export semantics, performance probes, and manual QA docs.

The feature is intentionally still conservative around third-party plugin parameters and generic pitch/MIDI fallbacks. The stable V1 semantic playback target is Simple Osc Complex, plus safe mixer routes for volume, pan, and send level.

## Implemented Surface

- Clip-owned expression state on MIDI clips.
- Default Volume and Pitch lanes.
- Expression lane creation, rename, enable/disable, polarity, routes, undo/redo, serialization, and migration.
- Phrase envelope clips tied to selected source notes.
- Cyclic/LFO expression clips tied to selected phrase regions.
- Pitch slurs, polyphonic/register-paired slur blocks, shared block edits, and per-voice overrides.
- Pitch-layer vibrato with phrase-wide and per-voice metadata.
- Release ghost support for first-party synth release tails.
- Expression overlay rendering in the piano roll.
- Expanded phrase envelope controls.
- Route picker/editor for available expression destinations.
- Prepared expression render model with fingerprints.
- First-party Simple Osc Complex native expression playback for modulation, slurs, vibrato/pitch offsets, and scheduled note events.
- Mixer expression playback for track volume, pan, and send level.
- Optional MIDI CC export for expression routes targeting MIDI CC.
- Lane preset/settings-copy helpers that do not copy note-bound phrase objects as detached clips.
- Dense expression performance probes and manual QA coverage.

## Resolved Architecture Questions

Expression state lives on `MidiClip` for V1.

Reusable track/project presets are limited to lane settings and routing snapshots. Note-bound phrase objects are not copied as free-floating clips.

Prepared control streams use a conservative fixed subdivision today.

The current prepared route step is `ticksPerQuarterNote / 16` in playback sync. This is intentionally simple and deterministic. A later cache can make this adaptive or per-destination if needed.

Generic first-party routes use native device modulation inputs.

Simple Osc Complex receives semantic expression data directly instead of pretending expression is ordinary automation. Mixer routes use existing safe mixer automation application paths on the message/control thread.

Third-party plugin parameter destinations are modeled but not played back.

This is intentional until parameter identity, state protection, and VST wipe regressions have a dedicated safe test-plugin path.

Expression edits preview through prepared sync boundaries for now.

Display-only expression editing should continue moving toward more precise dirty categories so UI edits do not force unnecessary playback graph work.

## Deferred Work

- Per-clip/lane prepared expression cache reuse by fingerprint.
- More precise expression dirty categories in AppServices/UI command paths.
- Third-party plugin parameter expression playback with state-wipe protection.
- MIDI CC route playback materialization for external/third-party instrument tracks.
- Pitch-bend fallback for non-first-party synths.
- Project-aware MIDI export wiring in the UI so warnings can be surfaced to the user.
- A broad preset browser for reusable lane routing/mapping.
- Manual QA of the full workflow in the actual app, including save/load and real playback listening.

## Performance Notes

Prompt 22 added the measured full performance pass in:

- `docs/performance-audit/20_EXPRESSION_MODE_FULL_PERFORMANCE_PASS.md`

Representative debug results from that pass:

- 1,000 notes / 8 lanes prepared expression render model: 19.984 ms.
- 1,000 notes / 8 lanes native route stream installation: 0.186 ms.
- 1,000 notes / 8 lanes full sync: 30.237 ms.
- 1,000 notes / 8 lanes playback start: 208.286 ms.

Playback start remains the largest measured expression-adjacent spike, but the trace suggests the cost is broader than expression preparation itself.

## Validation Commands

The final prompt-pack slices used these focused checks:

```sh
cmake --build build --target tsq_core_tests -j 6
cmake --build build --target tsq_engine_integration_tests -j 6
cmake --build build --target tsq_app -j 6
./build/tests/tsq_core_tests "*[expression]*"
./build/tests/tsq_core_tests "*Expression*"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_engine_integration_tests "*[expression][ui]*"
./build/tests/tsq_engine_integration_tests "*[expression][playback][mixer]*"
./build/tests/tsq_engine_integration_tests "*[expression][perf][sync]*"
```

Existing headless JUCE assertion logging appears during integration startup and component paint probes.

## Product Reminder

Expression Mode should remain a musical performance layer.

Stored expression data should describe musical intent. Playback, export, and first-party devices should translate that intent into clean control data at the boundary where it is needed.
