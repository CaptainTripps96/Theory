# Expression System Hardening Prompt Pack

Created: 2026-06-16

Source docs:
- `docs/EXPRESSION_MODE_IMPLEMENTATION_PROMPT_PACK.md`
- `docs/TheorySequencer_Expression_Mode_Spec_v2.txt`
- `docs/AUDIO_THREAD_AUDIT.md`

This is the follow-up prompt pack for hardening the implemented Expression Mode system. It is intentionally context-window-friendly: each prompt is a bounded pass with its own read list, goal, tasks, acceptance criteria, and handoff notes.

The rule for every prompt remains:

```text
Pristine audio path is king.
Performance is a very close second.
Musical intent stays editable and non-destructive.
```

## Current Audit Snapshot

The expression audit found these priority risks:

1. Looped native Simple Osc clips reuse the same voice IDs across repetitions.
2. Native Simple Osc playback has no note chasing when playback starts inside an already-active note.
3. Simple Osc render reads/rebuilds patch state from a `ValueTree` in the audio callback.
4. First-party devices force full Tracktion graph rebuilds instead of in-place expression sync.
5. Prepared expression render data drops some slur/vibrato block and override metadata.
6. Expression note references can become orphaned when referenced notes are deleted.
7. Several UI expression edits still commit whole expression-state replacements.
8. Route playback/export support is narrower than the long-term spec.
9. Vibrato drawing does not yet reflect all playback parameters.

## Driver Instructions

At the start of each prompt, read this document plus the prompt-specific files. At the end of each prompt, append a status note with:

- files changed
- tests run
- behavior verified
- remaining risks
- next prompt recommendation

Do not combine prompts unless the previous prompt is fully implemented, tests are green, and there is still enough context budget to verify the next change carefully.

---

# Prompt 01 - Native Simple Osc Note Identity And Note Chasing

## Status

Implemented 2026-06-16.

## Goal

Make native Simple Osc expression playback correct when clips loop and when playback starts in the middle of a clip. A loop repetition must not reuse the same native voice handle as another repetition, and starting playback inside a sustained native note should reconstruct the voice state instead of waiting silently for the next note-on.

## Read

- `src/engine/TracktionPlaybackEngine.cpp`
- `src/engine/devices/SimpleOscComplexTracktionPlugin.h`
- `src/engine/devices/SimpleOscComplexTracktionPlugin.cpp`
- `src/core/devices/SimpleOscComplexSynth.h`
- `src/core/devices/SimpleOscComplexSynth.cpp`
- `src/core/sequencing/ClipLoop.*`
- `src/core/sequencing/MidiClip.*`
- `tests/core/SimpleOscComplexSynthTest.cpp`
- `tests/integration/ExpressionBaselinePerformanceProbeTest.cpp`

## Tasks

1. Include loop repetition identity in the native Simple Osc note handle derivation.
2. Keep source and destination handles consistent for slur and vibrato events within the same repetition.
3. Add native note chasing for playback starts/seeks into the middle of scheduled notes.
4. Preserve legato slur ownership rules: source note-offs stay suppressed for valid legato sources, and destination note-offs own release.
5. Keep all event preparation off the audio thread.
6. Add focused regression tests for duplicate IDs/repetitions and mid-note playback start behavior.

## Acceptance Criteria

- Looped clips schedule distinct native note IDs per repetition.
- A note already active at the playback start can sound without waiting for another note-on.
- Existing slur and vibrato tests continue to pass.
- The fix does not introduce project-model traversal or allocation inside the synth render callback beyond existing event-vector reads.
- Targeted core and integration tests pass.

## Deferred

- Perfect slur/vibrato state reconstruction when seeking into the middle of a slur is acceptable to defer if basic sustained-note chasing is correct and documented.
- Audio-thread patch/state caching belongs to Prompt 02.

## Implementation Notes - 2026-06-16

Files changed:
- `src/engine/TracktionPlaybackEngine.*`
- `src/engine/devices/SimpleOscComplexTracktionPlugin.*`
- `src/core/devices/SimpleOscComplexSynth.cpp`
- `tests/core/SimpleOscComplexSynthTest.cpp`
- `tests/integration/ExpressionBaselinePerformanceProbeTest.cpp`

What changed:
- Native Simple Osc note handles now include the loop repetition start tick, so each visible loop repetition gets distinct voice IDs.
- Simple Osc expression playback now detects first render/seeks/discontinuities and chases prepared note, slur, and pitch-offset events up to the render start time.
- The synth defensively releases every active voice sharing a note ID if a duplicate ID ever reaches DSP.
- Concrete debug hooks were added to `TracktionPlaybackEngine` for native Simple Osc event IDs, active voice count, and playhead chase verification.

Tests run:
- `cmake --build build --target tsq_core_tests tsq_engine_integration_tests -j 6`
- `./build/tests/tsq_core_tests "[devices][simple-osc-complex][expression]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][simple-osc]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression]"`

Result:
- All targeted tests passed.
- The broad integration run passed 341 assertions in 21 test cases.
- The run still prints the existing JUCE assertions from plugin format manager/component cleanup; no new failure was observed.

Remaining risks:
- Mid-slur chase reconstructs the active note/slur ownership but does not advance the slur curve to the exact in-progress phase.
- Chased notes restart their envelope from note-on rather than reconstructing exact envelope age. Prompt 02 should make the render snapshot path cleaner before deeper chase fidelity work.

## Prompt 01 Audit Addendum - 2026-06-18

Files changed:
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/engine/TracktionPlaybackEngine.h`
- `src/engine/devices/SimpleOscComplexTracktionPlugin.cpp`
- `src/engine/devices/SimpleOscComplexTracktionPlugin.h`
- `tests/integration/ExpressionBaselinePerformanceProbeTest.cpp`

What changed:
- Reconfirmed the Prompt 01 invariant that native Simple Osc playback must use prepared native note events for every Simple Osc MIDI clip, including clips with no expression objects.
- Restored all native Simple Osc clips to the scheduled-note path instead of falling back to ordinary Tracktion MIDI note materialization for no-expression clips.
- Kept empty/wake Tracktion MIDI clips for native Simple Osc tracks so the graph still has a clip surface without duplicating note playback.
- Added focused debug counters for native Simple Osc render callbacks, render callbacks with MIDI, render callbacks while playing, MIDI note-ons received through Tracktion, and last render time range.
- Added a regression proving a no-expression Simple Osc clip schedules native note IDs, avoids duplicate Tracktion MIDI-note materialization, and can be chased into an active voice at the playhead.

Behavior verified:
- Plain no-expression Simple Osc notes are represented by stable native scheduled note IDs.
- Native note chasing works for a plain sustained note without requiring a pitch slur or vibrato object.
- Slur-chain and broader expression behavior remained green after restoring the all-native scheduling path.

Tests run:
- `cmake --build build --target tsq_engine_integration_tests -j 6`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][simple-osc]"`
- `./build/tests/tsq_engine_integration_tests "*[slur]*"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression]"`
- `./build/tests/tsq_core_tests "[devices][simple-osc-complex][expression]"`

Result:
- Simple Osc expression integration passed 43 assertions in 6 test cases.
- Slur integration passed 102 assertions in 5 test cases.
- Broad expression integration passed 441 assertions in 27 test cases.
- Core Simple Osc expression tests passed 286 assertions in 8 test cases.

Remaining risks:
- The headless live-transport probe rendered idle callbacks with `context.isPlaying == false`, so it is not a reliable proof of audible transport playback by itself.
- Prompt 02 should be re-audited next to ensure the render-side patch/modulation snapshot path still stays allocation-free and does not depend on ordinary Tracktion MIDI delivery.

---

# Prompt 02 - Realtime-Safe Simple Osc Patch And Modulation Snapshots

## Status

Implemented 2026-06-17.

## Goal

Remove avoidable allocation/string/property lookup from Simple Osc audio rendering. Patch changes and expression modulation streams should be converted into render-ready numeric snapshots outside the audio callback.

## Read

- Prompt 01 status note
- `src/engine/devices/SimpleOscComplexTracktionPlugin.*`
- `src/core/devices/FirstPartyDeviceRegistry.*`
- `src/core/devices/SimpleOscComplexSynth.*`
- `docs/AUDIO_THREAD_AUDIT.md`
- `tests/core/SimpleOscComplexSynthTest.cpp`

## Tasks

1. Cache the base patch when first-party state changes instead of rebuilding it every audio block.
2. Replace render-path parameter string comparisons with numeric parameter IDs or pre-resolved setters.
3. Store expression modulation streams in a render-friendly structure with efficient cursoring or indexed lookup.
4. Add tests/probes that fail if basic render paths require state reconstruction per block.
5. Update `docs/AUDIO_THREAD_AUDIT.md`.

## Acceptance Criteria

- `applyToBuffer` no longer rebuilds `FirstPartyDeviceState` from `ValueTree`.
- Render modulation lookup avoids repeated string/property work.
- Existing expression playback tests remain green.

## Implementation Notes - 2026-06-17

Files changed:
- `src/engine/devices/SimpleOscComplexTracktionPlugin.*`
- `src/engine/TracktionPlaybackEngine.*`
- `tests/integration/ExpressionBaselinePerformanceProbeTest.cpp`
- `docs/AUDIO_THREAD_AUDIT.md`

What changed:
- Native Simple Osc now refreshes its base patch from the Tracktion plugin `ValueTree` during initialisation/control preparation instead of rebuilding first-party device state inside `applyToBuffer`.
- Expression modulation streams are resolved from parameter IDs to numeric Simple Osc modulation targets before playback rendering.
- Modulation segments are validated, sorted, and stored as prepared streams; render-time lookup uses indexed start-time lookup and a target switch instead of string/property work.
- Debug probes expose prepared modulation stream count and patch-state refresh count for native Simple Osc regression coverage.

Behavior verified:
- First-party expression routes into Simple Osc prepare a native modulation stream before playback.
- Playback start does not cause repeated native Simple Osc patch-state reconstruction.

Tests run:
- `cmake --build build --target tsq_core_tests tsq_engine_integration_tests -j 6`
- `./build/tests/tsq_core_tests "[devices][simple-osc-complex][expression]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][simple-osc]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression]"`

Result:
- All targeted tests passed.
- The broad integration run passed 347 assertions in 22 test cases.
- The run still prints the existing JUCE assertions from plugin format manager/component cleanup and the existing `VstStateRegressionTest.cpp` unused-variable warning; no new failure was observed.

Remaining risks:
- The prepared modulation lookup is block/segment-start sampled, matching the current Simple Osc patch update cadence. Future sample-accurate parameter modulation will need a deeper synth-side modulation path.
- First-party parameter edits still depend on the current sync/rebuild path. Prompt 03 should add in-place sync for first-party device state and expression-only edits.

## Prompt 02 Audit Addendum - 2026-06-18

Files changed:
- `src/engine/devices/SimpleOscComplexTracktionPlugin.cpp`
- `docs/AUDIO_THREAD_AUDIT.md`
- `docs/EXPRESSION_SYSTEM_HARDENING_PROMPT_PACK.md`

What changed:
- Removed an allocation-capable `AudioBuffer::setSize()` call from `SimpleOscComplexTracktionPlugin::applyToBuffer()`. If Tracktion ever supplies fewer than the declared stereo output channels, the plugin now clears the requested block and returns instead of resizing the host buffer inside render.
- Reconfirmed that Simple Osc base patch state is converted from `ValueTree` only on initialisation/control paths, not per block.
- Reconfirmed that expression route destinations are resolved to numeric Simple Osc modulation targets before render, and that render-time route lookup does not depend on string/property comparisons.
- Updated `docs/AUDIO_THREAD_AUDIT.md` to describe the two prepared playback paths accurately: hosted-plugin tracks use Tracktion MIDI sequence materialization, while native Simple Osc tracks use prepared scheduled note/slur/pitch-offset vectors.

Behavior verified:
- Static scan of `SimpleOscComplexTracktionPlugin.cpp` found no remaining render-path host-buffer resize.
- Render-path `ValueTree`, property, and string conversion work remains outside `applyToBuffer()`.
- Existing Simple Osc expression and broad expression behavior remained green.

Tests run:
- `cmake --build build --target tsq_engine_integration_tests tsq_core_tests -j 6`
- `./build/tests/tsq_core_tests "[devices][simple-osc-complex][expression]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][simple-osc]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression]"`

Result:
- Core Simple Osc expression tests passed 286 assertions in 8 test cases.
- Simple Osc expression integration passed 43 assertions in 6 test cases.
- Broad expression integration passed 441 assertions in 27 test cases.
- The integration runs still print the existing Debug JUCE assertions from plugin format manager/component cleanup; no test failure was observed.

Remaining risks:
- Current route modulation is still block/segment-start patch updating, not sample-accurate destination modulation.
- Prepared event and modulation vectors must continue to be replaced only from sync/control paths as Prompt 03 and later live-edit work evolves.
- Render debug atomics are low overhead, but should stay easy to remove or compile-gate if profiling shows they matter.

---

# Prompt 03 - First-Party Device In-Place Expression Sync

## Status

Implemented 2026-06-17.

## Goal

Allow first-party device projects to use in-place playback sync for expression and clip edits that do not require a full Tracktion graph rebuild.

## Read

- Prompt 01 and 02 status notes
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/core/sequencing/DeviceChain.*`
- `tests/integration/TracktionSyncPerformanceProbeTest.cpp`
- `tests/integration/ExpressionBaselinePerformanceProbeTest.cpp`

## Tasks

1. Remove the blanket `slot.isFirstPartyDevice()` in-place sync rejection.
2. Reuse existing native device plugin instances when device-chain topology is unchanged.
3. Refresh native pitch events and modulation streams during in-place sync.
4. Add regression coverage proving first-party expression edits sync in place.

## Acceptance Criteria

- Simple Osc expression edits no longer force full edit graph rebuilds when topology is unchanged.
- The sync status/perf trace identifies in-place sync for covered cases.
- Existing VST state preservation tests remain green.

## Implementation Notes - 2026-06-17

Files changed:
- `src/engine/devices/SimpleOscComplexTracktionPlugin.*`
- `src/engine/TracktionPlaybackEngine.cpp`
- `tests/integration/TracktionSyncPerformanceProbeTest.cpp`

What changed:
- `TracktionPlaybackEngine::canSyncProjectInPlace()` no longer rejects supported first-party Simple Osc slots solely because they are first-party devices.
- In-place sync now verifies native Simple Osc track/slot/type/kind/enabled-state identity, then reuses the existing native plugin when topology is unchanged.
- Native Simple Osc now exposes a control-path `setFirstPartyDeviceState()` method so in-place sync can refresh cached patch state without rebuilding the Tracktion graph.
- In-place sync refreshes native first-party device state, prepared pitch events, and prepared modulation streams before dispatching Tracktion updates.
- Added a regression proving a Simple Osc expression route edit syncs in place and keeps a prepared native modulation stream.

Tests run:
- `cmake --build build --target tsq_core_tests tsq_engine_integration_tests -j 6`
- `./build/tests/tsq_engine_integration_tests "First-party Simple Osc expression edits sync in place"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][sync]"`
- `./build/tests/tsq_engine_integration_tests "[integration][sync]"`
- `./build/tests/tsq_engine_integration_tests "[integration][vst3][plugin-state]"`

Result:
- All targeted tests passed.
- VST plugin-state regression passed 440 assertions in 1 test case.
- Runs still print the existing JUCE plugin-format assertion; no new failure was observed.

Remaining risks:
- In-place eligibility still uses conservative routing/device checks. Device order changes should continue to be treated carefully before broadening this path further.
- Only Simple Osc Complex is supported as a native first-party playback device today; additional first-party devices will need matching identity and state-refresh hooks.

## Prompt 03 Audit Addendum - 2026-06-18

Files changed:
- `tests/integration/TracktionSyncPerformanceProbeTest.cpp`
- `docs/EXPRESSION_SYSTEM_HARDENING_PROMPT_PACK.md`

What changed:
- Re-audited the Prompt 03 in-place path: `syncProject()` still chooses `syncProjectInPlace()` when graph topology is unchanged, and that path still refreshes native first-party device state, prepared pitch events, and prepared expression route streams before dispatching Tracktion updates.
- Reconfirmed `canSyncProjectInPlace()` no longer rejects supported first-party Simple Osc slots just because they are first-party devices; it checks track/slot/type/kind/enabled-state identity instead.
- Added focused regression coverage for a Simple Osc first-party parameter edit. The test mutates `osc.pm.amount`, syncs again, verifies the status reports in-place sync, verifies the prepared native modulation stream remains present, and verifies the cached patch refresh count advances.

Behavior verified:
- Simple Osc expression route edits still sync in place and keep prepared native modulation streams.
- Simple Osc parameter edits now have direct regression coverage for in-place sync and cached patch refresh.
- The broader sync suite and VST plugin-state preservation regression remained green.

Tests run:
- `cmake --build build --target tsq_engine_integration_tests tsq_core_tests -j 6`
- `./build/tests/tsq_engine_integration_tests "First-party Simple Osc parameter edits sync in place"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][sync]"`
- `./build/tests/tsq_engine_integration_tests "[integration][sync]"`
- `./build/tests/tsq_engine_integration_tests "[integration][vst3][plugin-state]"`

Result:
- New Simple Osc parameter in-place sync regression passed 17 assertions in 1 test case.
- Expression sync passed 66 assertions in 3 test cases.
- Broad sync passed 91 assertions in 4 test cases.
- VST plugin-state regression passed 408 assertions in 1 test case.
- Runs still print the existing Debug JUCE plugin-format assertion; no new failure was observed.

Remaining risks:
- In-place eligibility remains intentionally conservative for return tracks, sends, sidechains, MIDI routing, device ordering, and unsupported first-party device types.
- Simple Osc parameter edits refresh the cached patch at sync/control cadence; this is not live sample-accurate parameter modulation.
- The current debug assertions prove state refresh/count behavior, not rendered audio timbre differences.

---

# Prompt 04 - Complete Prepared Expression Metadata

## Status

Implemented 2026-06-17.

## Goal

Make prepared expression render data represent all stored musical metadata required for stable fingerprints, sync decisions, export warnings, and future renderers.

## Read

- `src/core/sequencing/PreparedExpressionRenderModel.*`
- `src/core/sequencing/Expression.*`
- `src/core/serialization/ProjectSerializer.cpp`
- `tests/core/PreparedExpressionRenderModelTest.cpp`
- `tests/core/ProjectSerializationTest.cpp`

## Tasks

1. Add slur block ID to prepared pitch slur events.
2. Add vibrato block ID and voice overrides to prepared vibrato events.
3. Include the new fields in prepared fingerprints.
4. Add tests proving fingerprint/model changes when override metadata changes.

## Acceptance Criteria

- Prepared model no longer loses slur/vibrato override metadata.
- Serialization and prepared-model tests cover the full metadata path.

## Implementation Notes - 2026-06-17

Files changed:
- `src/core/sequencing/PreparedExpressionRenderModel.*`
- `tests/core/PreparedExpressionRenderModelTest.cpp`
- `tests/core/ProjectSerializationTest.cpp`

What changed:
- Prepared pitch slur events now carry optional expression block IDs.
- Prepared vibrato events now carry optional expression block IDs and per-note voice overrides.
- Prepared expression fingerprints now include slur block IDs, vibrato block IDs, and every vibrato voice override field.
- Prepared-model tests now assert the metadata survives preparation and that fingerprints change when slur/vibrato override metadata changes.
- Serialization round-trip coverage now checks vibrato block ID and all stored voice override fields.

Tests run:
- `cmake --build build --target tsq_core_tests -j 6`
- `./build/tests/tsq_core_tests "*Prepared expression*"`
- `./build/tests/tsq_core_tests "Expression state round trips through project serializer"`
- `./build/tests/tsq_core_tests "*expression*"`

Result:
- All targeted tests passed.
- Broad core expression run passed 683 assertions in 44 test cases.

Remaining risks:
- Prepared metadata now preserves the stored fields, but downstream renderers still need to opt into any newly exposed voice override semantics they intend to use.

## Prompt 04 Audit Addendum - 2026-06-18

Files changed:
- `tests/core/PreparedExpressionRenderModelTest.cpp`
- `docs/EXPRESSION_SYSTEM_HARDENING_PROMPT_PACK.md`

What changed:
- Re-audited prepared expression metadata structures, preparation, fingerprints, and project serialization for pitch slur and vibrato semantic metadata.
- Reconfirmed prepared pitch slur events carry `blockId` and `hasVoiceOverride`, and those fields are included in the prepared lane fingerprint.
- Reconfirmed prepared vibrato events carry `blockId` and all voice override fields, and both event-level block IDs and per-override fields are included in the prepared lane fingerprint.
- Strengthened prepared-model coverage so vibrato voice override attack/release timing is asserted after preparation.
- Added fingerprint assertions for vibrato block ID edits and vibrato voice override timing edits, closing a coverage gap where only slur block ID and one vibrato override value were directly tested.

Behavior verified:
- Prepared model no longer loses slur/vibrato block and override metadata.
- Serializer round-trip coverage still preserves slur block IDs, vibrato block IDs, and all stored vibrato voice override fields.
- Prepared fingerprints change when slur block IDs, vibrato block IDs, vibrato override amplitude, or vibrato override timing changes.

Tests run:
- `cmake --build build --target tsq_core_tests -j 6`
- `./build/tests/tsq_core_tests "*Prepared expression*"`
- `./build/tests/tsq_core_tests "Expression state round trips through project serializer"`
- `./build/tests/tsq_core_tests "*expression*"`

Result:
- Prepared expression tests passed 88 assertions in 6 test cases.
- Expression serializer round-trip passed 48 assertions in 1 test case.
- Broad core expression suite passed 743 assertions in 46 test cases.

Remaining risks:
- Prepared metadata is complete for the currently stored slur/vibrato fields, but renderers still consume only the subset they have explicitly implemented.
- Fingerprints are deterministic reuse keys, not semantic compatibility guarantees across future schema changes; new fields must keep being added to hashes as they become render-relevant.

---

# Prompt 05 - Expression Reference Lifecycle And Cleanup

## Status

Implemented 2026-06-17.

## Goal

Prevent orphaned expression objects when MIDI notes are deleted, pasted, duplicated, or transformed.

## Read

- `src/core/commands/ProjectMutationCommands.cpp`
- `src/core/commands/ExpressionCommands.*`
- `src/core/sequencing/Expression.*`
- `src/core/sequencing/PitchExpressionEvaluation.*`
- `src/ui/PianoRollComponent.cpp`
- `tests/core/CommandStackTest.cpp`

## Tasks

1. Add a central helper to remove or repair expression objects that reference missing notes.
2. Integrate cleanup into note deletion and any bulk note mutation commands where references become invalid.
3. Preserve undo/redo of both note and expression cleanup together.
4. Add tests for slur/vibrato/phrase cleanup after note deletion.

## Acceptance Criteria

- Deleting a referenced note does not leave invisible serialized slurs/vibrato objects.
- Undo restores the note and expression references.
- Pitch evaluation never throws from ordinary post-command project state.

## Implementation Notes - 2026-06-17

Files changed:
- `src/core/sequencing/ExpressionReferenceCleanup.*`
- `src/core/commands/DeleteNoteCommand.h`
- `src/core/commands/ChordStackingCommand.h`
- `src/core/commands/ArpeggiateSelectionCommand.h`
- `src/core/commands/ProjectMutationCommands.cpp`
- `src/core/CMakeLists.txt`
- `tests/core/CommandStackTest.cpp`

What changed:
- Added a central expression-reference cleanup helper for MIDI clips.
- Cleanup removes phrase envelopes, cyclic expression clips, pitch slurs, and vibrato expressions whose source/destination note references no longer exist.
- Cleanup repairs surviving vibrato expressions by dropping voice overrides that point at missing notes.
- Delete Note, Remove Highest Chord Tone, and Arpeggiate Selection now snapshot expression state, mutate notes, run cleanup, and restore the previous expression state on undo.
- Added regression coverage proving delete-note cleanup removes phrase/slur/vibrato references, repairs vibrato voice overrides, restores everything on undo, and leaves pitch expression evaluation non-throwing.

Tests run:
- `cmake --build build --target tsq_core_tests -j 6`
- `./build/tests/tsq_core_tests "Delete note command removes and restores expression references"`
- `./build/tests/tsq_core_tests "*command*"`
- `./build/tests/tsq_core_tests "*expression*"`
- `./build/tests/tsq_core_tests`

Result:
- All targeted tests passed.
- Full core test run passed 6034 assertions in 252 test cases.

Remaining risks:
- Undo-only removals for note-creation commands still rely on normal command-stack ordering, where expression edits are undone before the note creation they depended on.
- Piano-roll paste/duplicate currently creates fresh notes without copying note-bound expression objects, so no missing-reference cleanup is needed there yet. If expression copy/paste is added later, it should either remap note IDs or call the cleanup helper after remapping.

## Prompt 05 Audit Addendum - 2026-06-18

Files changed:
- `tests/core/CommandStackTest.cpp`
- `docs/EXPRESSION_SYSTEM_HARDENING_PROMPT_PACK.md`

What changed:
- Re-audited the central cleanup helper and command integrations for note-reference lifecycle safety.
- Reconfirmed `removeExpressionReferencesToMissingNotes()` removes phrase envelopes, cyclic expression clips, pitch slurs, and vibrato expressions that reference missing notes, and repairs surviving vibrato expressions by dropping missing-note voice overrides.
- Reconfirmed Delete Note, Remove Highest Chord Tone, and Arpeggiate Selection snapshot expression state, mutate notes, run cleanup, and restore the previous expression state on undo.
- Added focused regression coverage for Remove Highest Chord Tone cleanup/undo, including phrase envelope, cyclic clip, slur, vibrato voice override repair, and pitch evaluation safety.
- Added focused regression coverage for Arpeggiate Selection cleanup/undo using a real note-ID disappearance case, so the test proves cleanup rather than merely preserving all original IDs.

Behavior verified:
- Deleting a referenced note still removes stale expression objects and repairs surviving vibrato overrides.
- Removing a highest chord tone no longer risks leaving expression references to the removed top note.
- Arpeggiating a selection that drops an original note ID no longer leaves phrase/slur/vibrato override references to that missing note.
- Undo restores both notes and expression references for all covered cleanup commands.
- Pitch expression evaluation remains non-throwing after cleanup.

Tests run:
- `cmake --build build --target tsq_core_tests -j 6`
- `./build/tests/tsq_core_tests "Remove highest chord tone command cleans and restores expression references"`
- `./build/tests/tsq_core_tests "Arpeggiate selection command cleans and restores expression references"`
- `./build/tests/tsq_core_tests "Delete note command removes and restores expression references"`
- `./build/tests/tsq_core_tests "*command*"`
- `./build/tests/tsq_core_tests "*expression*"`
- `./build/tests/tsq_core_tests`

Result:
- Remove Highest Chord Tone cleanup regression passed 24 assertions in 1 test case.
- Arpeggiate Selection cleanup regression passed 22 assertions in 1 test case.
- Delete Note cleanup regression passed 30 assertions in 1 test case.
- Broad command suite passed 551 assertions in 63 test cases.
- Broad core expression suite passed 789 assertions in 48 test cases.
- Full core suite passed 6110 assertions in 255 test cases.

Remaining risks:
- Commands that only replace notes while preserving note IDs intentionally do not run cleanup; that is correct for current expression references, but future expression objects tied to note timing/pitch rather than ID existence may need repair hooks.
- Paste/duplicate remains safe only because note-bound expression objects are not copied yet. If expression copy/paste is added, note IDs must be remapped before cleanup.

---

# Prompt 06 - Granular UI Expression Commands

## Status

Implemented 2026-06-17.

## Goal

Move piano-roll expression edits away from coarse whole-state replacement and onto focused command/AppServices APIs.

## Read

- Prompt 05 status note
- `src/ui/PianoRollComponent.cpp`
- `src/app/AppServices.*`
- `src/core/commands/ExpressionCommands.*`
- `tests/integration/ExpressionModeUiShellTest.cpp`

## Tasks

1. Replace slur creation/deletion/edit commits with granular commands.
2. Replace vibrato creation/deletion/edit commits with granular commands.
3. Keep gesture-based edits as single undo entries.
4. Add UI/integration tests for undo labels and exact mutation behavior.

## Acceptance Criteria

- Whole-state replacement is reserved for true full-state operations.
- Common expression UI edits use specific command classes.
- Existing UI expression tests remain green.

## Implementation Notes - 2026-06-17

Files changed:
- `src/core/commands/ExpressionCommands.*`
- `src/app/AppServices.*`
- `src/ui/PianoRollComponent.cpp`
- `tests/integration/ExpressionModeUiShellTest.cpp`

What changed:
- Added granular replace/update commands for phrase envelopes, cyclic expression clips, pitch slurs, batched pitch slurs, and vibrato expressions.
- Added AppServices wrappers for the new granular expression commands.
- Piano-roll slur creation now uses a batched `AddPitchSlursCommand`, so register-paired chord slur blocks create as one undo entry instead of relying on whole-state replacement.
- Piano-roll slur edits now use batched pitch-slur replacement while preserving lane order, shared block edits, and per-voice overrides.
- Piano-roll vibrato creation, edits, deletes, and voice-override debug paths now use targeted vibrato commands.
- Phrase envelope and cyclic expression keyboard/panel commits now use specific replace/remove commands where practical; live phrase-envelope slider previews still write directly to the in-memory clip until gesture commit.
- Integration tests now assert one undo entry for slur/vibrato create/edit/delete flows and verify exact undo/redo mutation behavior.

Tests run:
- `cmake --build build --target tsq_core_tests tsq_engine_integration_tests -j 6`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][ui][slur]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][ui]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][sync]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression]"`
- `./build/tests/tsq_core_tests "*Expression object commands*"`
- `./build/tests/tsq_core_tests "*expression*"`
- `./build/tests/tsq_core_tests "*command*"`
- `./build/tests/tsq_core_tests`

Result:
- All targeted and broad expression tests passed.
- Full core test run passed 6034 assertions in 252 test cases.
- Broad expression integration run passed 392 assertions in 23 test cases.

Remaining risks:
- The current automated surface checks undo depth and exact mutation behavior, but there is no rendered undo-history label UI to assert against yet. The underlying command names are now specific enough for such a UI.
- Route range edits and phrase-envelope gesture finalization still use full-state commits. They are intentionally left for the route/final-QA prompt or a future gesture-command pass because they are not slur/vibrato-specific.
- Integration runs still print the existing JUCE plugin-format/component assertions; no test failure was associated with those assertions in this prompt.

## Prompt 06 Audit Addendum - 2026-06-19

Files changed:
- `src/core/commands/CommandStack.*`
- `src/ui/PianoRollComponent.cpp`
- `tests/core/CommandStackTest.cpp`
- `tests/integration/ExpressionModeUiShellTest.cpp`
- `docs/EXPRESSION_SYSTEM_HARDENING_PROMPT_PACK.md`

What changed:
- Audited the piano-roll expression edit paths against this prompt. Slur create/edit/delete already used granular pitch-slur commands, and route range plus phrase-envelope gesture finalization remained the only broad full-state UI exceptions.
- Added read-only `CommandStack::nextUndoName()` and `CommandStack::nextRedoName()` accessors so tests can assert the command label that an undo UI would show.
- Split expression create commits from edit commits in the piano roll for phrase envelopes, cyclic clips, and vibrato expressions. Brand-new expression objects now use `Add ...` commands; existing expression mutations continue to use `Replace ...` commands; removals continue to use `Remove ...` commands.
- Extended core and integration tests to assert undo/redo labels for phrase-envelope, cyclic, pitch-slur, and vibrato UI flows while preserving the existing one-entry undo and exact mutation checks.

Tests run:
- `cmake --build build --target tsq_core_tests tsq_engine_integration_tests -j 6`
- `./build/tests/tsq_core_tests "Expression object commands add remove and restore clip expression data"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][ui]"`
- `./build/tests/tsq_core_tests "*command*"`
- `./build/tests/tsq_core_tests "*expression*"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression]"`
- `./build/tests/tsq_core_tests`

Result:
- All targeted and broad expression tests passed.
- Full core test run passed 6127 assertions in 255 test cases.
- Broad expression integration run passed 488 assertions in 28 test cases.

Remaining risks:
- There is still no rendered undo-history UI to inspect, but the command stack now exposes the relevant labels for tests and future UI.
- Route range edits and phrase-envelope gesture finalization still use full-state commits. They remain intentional exceptions until a focused route/gesture command pass.
- Integration runs still print the existing JUCE plugin-format/component assertions; no test failure was associated with those assertions in this audit.

---

# Prompt 07 - Route Expansion, Visual Accuracy, And Final QA

## Goal

Close remaining expression feature gaps after the playback/audio architecture is solid.

## Read

- Prompt 01-06 status notes
- `docs/TheorySequencer_Expression_Mode_Spec_v2.txt`
- `src/core/sequencing/ExpressionDestinationRegistry.*`
- `src/core/midi/MidiExporter.*`
- `src/ui/PianoRollComponent.cpp`
- `tests/core/MidiExporterTest.cpp`
- `tests/integration/ExpressionModeUiShellTest.cpp`

## Tasks

1. Decide the next route targets: MIDI CC, pitch bend, third-party plugin parameter, or Tracktion automation.
2. Improve vibrato overlay drawing to reflect frequency, wave shape, fades, and overrides.
3. Add user-facing diagnostics for routes that are stored but not playback-mapped.
4. Update manual QA docs for expression playback, looped clips, slurs, vibrato, and export.

## Acceptance Criteria

- Unsupported route behavior is clear to users.
- Vibrato visuals are closer to actual playback.
- QA docs cover the first-party expression-critical workflows.

## Status Implemented 2026-06-18

Route target decision:
- Keep live playback support focused on mixer destinations and first-party device parameters.
- Add MIDI CC as the next plain-MIDI route target, with CC 1 Mod Wheel, CC 11 Expression, and CC 74 Brightness exposed as common export-oriented destinations.
- Leave generic Pitch/Pitch Bend and third-party plugin parameter destinations stored-only for now, with visible diagnostics rather than implying playback support.

Implementation notes:
- Added central runtime support metadata in `ExpressionDestinationRegistry` so each destination reports whether it is live-playback mapped, plain-MIDI-export mapped, stored-only, or unavailable.
- Routed MIDI export eligibility through that runtime support metadata so future route expansion has one source of truth.
- Added support labels/tooltips in the piano-roll expression routing panel: `Playback`, `Export only`, `Stored only`, and `Unavailable`.
- Improved vibrato overlay drawing so it follows the selected frequency division, cyclic wave shape, attack/release fades, phase, amplitude, and per-voice overrides more closely.
- Expanded manual QA coverage for expression playback, routing diagnostics, looped clips, slurs, vibrato, and MIDI export.

Files changed:
- `src/core/sequencing/ExpressionDestinationRegistry.h`
- `src/core/sequencing/ExpressionDestinationRegistry.cpp`
- `src/core/midi/MidiExporter.cpp`
- `src/ui/PianoRollComponent.cpp`
- `tests/core/ExpressionDestinationRegistryTest.cpp`
- `docs/MANUAL_QA_CHECKLIST.md`

Verification:
- `cmake --build build --target tsq_core_tests tsq_engine_integration_tests -j 6`
- `./build/tests/tsq_core_tests "Expression destination registry*"`
- `./build/tests/tsq_core_tests "*MIDI exporter*"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][ui]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][sync]"`
- `./build/tests/tsq_core_tests`

Result:
- Build passed.
- Focused destination-registry tests passed 41 assertions in 4 test cases.
- Focused MIDI-exporter tests passed 63 assertions in 8 test cases.
- Expression UI integration slice passed 278 assertions in 13 test cases.
- Broad expression integration slice passed 392 assertions in 23 test cases.
- Expression sync integration slice passed 49 assertions in 2 test cases.
- Full core test run passed 6050 assertions in 253 test cases.

Remaining risks:
- MIDI CC live playback, generic Pitch/Pitch Bend playback/export, and third-party plugin parameter playback remain intentionally unmapped until a dedicated playback architecture pass.
- Route range edits and phrase-envelope gesture finalization still use full-state commits from earlier prompts.
- Vibrato visual accuracy is covered by paint/run tests and manual QA, but not yet by pixel-assertion tests.
- Integration tests still print the existing JUCE plugin-format/component assertion noise even though the test processes pass.

## Prompt 07 Audit Addendum - 2026-06-19

Files changed:
- `src/ui/PianoRollComponent.h`
- `src/ui/PianoRollComponent.cpp`
- `tests/core/MidiExporterTest.cpp`
- `tests/integration/ExpressionModeUiShellTest.cpp`
- `docs/EXPRESSION_SYSTEM_HARDENING_PROMPT_PACK.md`

What changed:
- Audited the Prompt 07 route-target decision and confirmed the current implementation keeps live playback focused on mixer and first-party destinations, exposes MIDI CC 1/11/74 as export-oriented destinations, and leaves generic pitch/pitch-bend plus third-party plugin parameters stored-only with diagnostics.
- Added piano-roll routing debug helpers for adding a destination by stable ID and reading displayed route support labels from the routing rows.
- Added an integration regression that verifies the route panel displays all user-facing support states: `Playback`, `Export only`, `Stored only`, and `Unavailable`.
- Added MIDI exporter regressions for two plain-MIDI edge cases: MIDI CC expression routes are skipped with a clear warning when expression CC export is disabled, and unavailable expression routes warn without emitting CC events.

Tests run:
- `cmake --build build --target tsq_core_tests tsq_engine_integration_tests -j 6`
- `./build/tests/tsq_core_tests "*MIDI exporter*"`
- `./build/tests/tsq_core_tests "Expression destination registry*"`
- `./build/tests/tsq_engine_integration_tests "Expression Mode routing UI labels playback export stored and unavailable routes"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][ui]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][sync]"`
- `./build/tests/tsq_core_tests`

Result:
- Build passed.
- Focused MIDI-exporter tests passed 81 assertions in 10 test cases.
- Focused destination-registry tests passed 41 assertions in 4 test cases.
- Focused route-label UI test passed 10 assertions in 1 test case.
- Expression UI integration slice passed 350 assertions in 16 test cases.
- Broad expression integration slice passed 498 assertions in 29 test cases.
- Expression sync integration slice passed 66 assertions in 3 test cases.
- Full core test run passed 6145 assertions in 257 test cases.

Remaining risks:
- MIDI CC live playback, generic Pitch/Pitch Bend playback/export, and third-party plugin parameter playback remain intentionally unmapped until a dedicated playback architecture pass.
- Route range edits and phrase-envelope gesture finalization still use full-state commits from earlier prompts.
- Vibrato visual accuracy is covered by behavioral and manual QA checks, but still does not have pixel-level overlay assertions.
- Integration tests still print the existing JUCE plugin-format/component assertions; no test failure was associated with those assertions in this audit.

---

# Post-Pack Reliability Addendum - Native Simple Osc Stopped Render Guard

## Status

Implemented 2026-06-19.

## Problem

Hosted VST instruments receive ordinary MIDI materialization during playback, but native Simple Osc clips are driven by prepared scheduled note/slur/pitch-offset vectors. The native plugin was processing those scheduled clip events during stopped live audio callbacks as well as during actual playback. That meant a newly created note could be consumed by the native synth while the transport was stopped, making Simple Osc behavior diverge from hosted VST behavior.

## Files Changed

- `src/engine/devices/SimpleOscComplexTracktionPlugin.*`
- `src/engine/TracktionPlaybackEngine.*`
- `tests/integration/ExpressionBaselinePerformanceProbeTest.cpp`

## What Changed

- Native Simple Osc scheduled clip events now run only when Tracktion reports live playback, or when an offline render context has a non-empty edit-time range.
- Stopped live callbacks can still render existing synth state and live MIDI, but they no longer chase or consume scheduled clip notes/slurs.
- Zero-length offline tail probes are prevented from retriggering timeline notes at beat zero.
- Added a native Simple Osc debug max-output-peak probe so integration tests can prove real audio samples were rendered, not only that voices became active.
- Strengthened the UI-created-note playback regression to verify stopped callbacks remain silent after note entry and playback produces nonzero Simple Osc output.

## Tests Run

- `cmake --build build --target tsq_engine_integration_tests -j 6`
- `./build/tests/tsq_engine_integration_tests "Native Simple Osc UI-created notes wake and render during playback"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][simple-osc]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression]"`
- `./build/tests/tsq_engine_integration_tests "[integration][expression][sync]"`
- `./build/tests/tsq_engine_integration_tests "[integration][vst3][plugin-state]"`
- `cmake --build out/build/debug --target tsq_app -j 6`
- `cmake --build build --target tsq_app -j 6`

## Result

- Focused UI-created-note playback regression passed 13 assertions in 1 test case.
- Simple Osc expression integration passed 90 assertions in 10 test cases.
- Broad expression integration passed 545 assertions in 33 test cases.
- Expression sync integration passed 66 assertions in 3 test cases.
- VST plugin-state regression passed 406 assertions in 1 test case.
- Debug app builds completed in `out/build/debug` and `build`.
- Existing JUCE debug assertion noise from plugin-format/component cleanup is still present, but no test failed.

## Remaining Risks

- The max-output-peak probe is a debug diagnostic, not a user-facing meter.
- Native Simple Osc still uses prepared vectors owned by the plugin instance; future live-edit performance work should continue toward a formally double-buffered render snapshot if edits while playing become more aggressive.
