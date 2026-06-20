# First-Party Devices And Simple Osc Complex Architecture

Last updated: 2026-06-15

This document captures the planning direction for TheorySequencer first-party devices and the first native synthesizer, `Simple Osc Complex`.

This is design work only. It does not claim an implementation exists yet.

## Purpose

TheorySequencer needs first-party instruments that can eventually speak the Expression Mode language directly. The goal is not only to make a built-in synth, but to create the native device contract that future expression lanes, slurs, vibrato, release ghosts, and per-note performance data can target.

The first synth should be useful on its own, but its deeper purpose is architectural:

- Prove the first-party device model.
- Prove inline device-chain editors.
- Prove structured patch serialization.
- Prove stable expression-ready parameters.
- Prove note-ID-aware polyphonic voice handling.
- Prepare for future pitch-lane slurs, vibrato, and release-aware expression selection.

## Product Direction

First-party devices should be native TheorySequencer devices, not ordinary third-party VSTs hidden behind plugin-state blobs.

They should:

- Live in the selected track's device chain.
- Show their editor inline in the device chain.
- Serialize patch state as structured project/package data.
- Expose stable parameter metadata.
- Expose expression-ready destinations.
- Support note-ID-aware playback.
- Eventually accept first-party expression instructions that third-party VSTs cannot understand.

The first first-party instrument is:

    Simple Osc Complex

It is a polyphonic native synth built around one complex oscillator, wavefolding, and an amp envelope.

## Browser Model

The left/browser area should distinguish external plugins from first-party devices.

Planned browser tabs:

- `Scales`
- `Plugins`
- `Devices`

Definitions:

- `Scales`: existing musical scale/custom scale content.
- `Plugins`: third-party scanned plugins, especially VST3 instruments and effects.
- `Devices`: first-party TheorySequencer devices, including native instruments and future native effects/MIDI devices.

The `Devices` tab should eventually show a curated catalog of built-in devices. These do not require scanning. Their metadata should be compiled or registered by the app at startup.

Initial `Devices` entry:

- `Simple Osc Complex`

Future examples:

- Native sampler
- Native drum synth
- Native MIDI tools
- Native modulation utilities
- Native audio effects

## Device Chain Layout

The device chain should use a fixed Ableton-like rack height when the lower panel is in device-chain mode.

Current lower panel behavior should become mode-sensitive:

- Piano Roll mode: resizable/draggable lower panel.
- Device Chain mode: fixed-height rack area.

First-party devices should usually show their full editor within this fixed rack height. A compact version is not required for `Simple Osc Complex`.

However, the architecture should not forbid future devices from having alternate presentation modes. Some future devices may need:

- Full inline editor.
- Collapsed strip.
- Selected-device expansion.
- Modal or detached auxiliary editor.

For v1, the important behavior is simple:

- First-party instrument editor is visible directly in the device chain.
- It is designed to be comfortable at rack height.
- It does not depend on an external plugin editor window.

## Core Model Direction

The current model has `DeviceChain`, `DeviceSlot`, and `PluginReference`. That works for third-party plugin slots, but first-party devices need a parallel native identity.

Recommended direction:

```text
DeviceSlot
  -> DeviceReference
      -> ThirdPartyPluginReference
      -> FirstPartyDeviceReference
```

Conceptually, a first-party device reference should include:

- Stable device type ID.
- User-facing name.
- Device category.
- Native patch/state payload.
- Bypass state.
- Optional UI state.

Example stable type ID:

```text
tsq.device.instrument.simple-osc-complex
```

The exact C++ type names can change, but the model should avoid treating native devices as fake VSTs.

## First-Party Device Definition

Each first-party device should be registered by a definition object.

Suggested responsibilities:

- Stable type ID.
- Display name.
- Device kind: instrument, audio effect, MIDI effect, utility.
- Factory default patch.
- Parameter metadata.
- Patch validation/migration.
- Editor component factory.
- Engine/DSP factory.
- Expression capability metadata.

Illustrative shape:

```text
FirstPartyDeviceDefinition
  typeId
  name
  kind
  parameterDefinitions
  createDefaultPatch()
  createEditor(...)
  createProcessor(...)
  expressionCapabilities
```

## Patch Serialization

First-party patch data should be structured and deterministic.

Avoid opaque plugin-state blobs for native devices. Native patch data should be:

- JSON-serializable.
- Versioned.
- Migratable.
- Diffable.
- Testable in pure core tests where possible.

Simple Osc Complex patch state should include:

- Patch schema version.
- Parameter values keyed by stable parameter ID.
- Voice mode data if added later.
- UI state only when it is genuinely useful and non-destructive.

Patch data should live with the device slot in the project model. If a `.tseq` package later stores larger device assets, the slot can reference package files, but Simple Osc Complex v1 should not need external assets.

## Parameter Contract

Every first-party device parameter should have stable metadata.

Required metadata:

- Stable parameter ID.
- Display name.
- Normalized default value.
- Plain value range.
- Units, if applicable.
- Discrete/continuous flag.
- Automatable flag.
- Expression-targetable flag.
- Smoothing policy.
- Display formatting.

Expression Mode should eventually route to these stable IDs.

Important principle:

    Parameter identity must not depend on UI order.

The UI can move controls around freely without breaking saved projects or expression routes.

## Expression Capability Contract

First-party instruments should expose capabilities beyond ordinary parameter automation.

The long-term expression contract should include:

- Stable parameter destinations.
- Note-ID-aware voice targeting.
- Pitch expression support.
- Slur/legato instruction support.
- Per-voice pitch offset support.
- Phrase vibrato support.
- Release-time metadata.
- Patch/static voice metadata useful to Expression Mode.

Expression Mode can then store musical intent and render to native instructions rather than raw automation.

## Note-ID-Aware Voice Model

The synth should be polyphonic in v1.

This is deliberately the hard path, because Expression Mode will eventually need polyphonic slurs and per-voice pitch expression.

The voice allocator should preserve a relationship between:

- MIDI note ID from the project clip.
- MIDI pitch.
- Voice instance.
- Note-on time.
- Note-off time.
- Release state.
- Future expression objects targeting that note ID.

This does not require full Expression Mode in v1. It does mean the playback path should avoid collapsing everything into anonymous MIDI note numbers too early.

Future expression-aware events may include:

- Note ID.
- Source note ID.
- Destination note ID.
- Slur block ID.
- Per-voice pitch curve.
- Legato/no-retrigger flag.
- Vibrato block ID.

## Release Metadata

First-party synth patches should expose their current release time.

Expression Mode will eventually use this for release ghost notes:

- Display low-opacity note tails in Expression Mode.
- Convert release milliseconds to timeline duration at the current tempo.
- Allow release tails to participate in marquee selection when Release Mode is enabled.

For Simple Osc Complex v1, this can be derived from the amp envelope release parameter.

If release is later modulated, v1 can still report the current/static patch release value.

## Playback Integration Options

There are two broad implementation options.

### Option A: Native Device Engine With Tracktion Adapter

TheorySequencer owns the first-party DSP and patch model directly. The Tracktion playback backend adapts native device output into the existing playback graph.

Advantages:

- Best semantic access for Expression Mode.
- Structured patch state by default.
- No fake plugin scanning or external editor lifecycle.
- Easier inline editor model.
- Easier note-ID-aware voice and expression events.

Risks:

- Requires careful integration with Tracktion's audio graph.
- May require a custom Tracktion plugin wrapper or internal audio path.

### Option B: First-Party JUCE/Tracktion Plugin

The synth is implemented as an internal plugin-like processor and hosted similarly to VSTs.

Advantages:

- Fits Tracktion's plugin list model.
- Audio/MIDI processing path may be more straightforward.
- Can reuse some plugin state/parameter pathways.

Risks:

- Temptation to treat native devices like opaque plugins.
- Harder to support expression semantics cleanly.
- Inline editor may fight the existing plugin-window model.
- Note-ID-aware expression can become an awkward side channel.

### Recommendation

Prefer a native first-party device model with an adapter into playback, even if the first playback implementation internally uses Tracktion plugin mechanics.

The public app/core model should remain native and expression-aware. Tracktion should be an implementation detail.

## Inline Editor Contract

A first-party device editor should be a JUCE component created by the native device definition.

It should receive:

- Device slot identity.
- Read-only parameter metadata.
- Current patch state.
- Parameter change callbacks.
- Optional UI state callbacks.
- App theme/style context.

It should not own the source of truth. Parameter changes should flow through app commands or a dedicated device-parameter edit path so undo/redo, dirtying, serialization, and playback sync remain coherent.

For Simple Osc Complex:

- No compact editor is required.
- The editor is designed for fixed rack height.
- Controls should be sliders where requested, especially the amp ADSR.
- The amp envelope should include a curve visualization.

## Simple Osc Complex V1

### Concept

Simple Osc Complex is a polyphonic native synth with one complex oscillator.

Signal concept:

```text
Triangle carrier
  <- phase modulation from ratio-tuned modulator oscillator
  -> multi-stage wavefolder
  -> amp envelope
  -> output
```

No filter is required for v1.

### Voice Architecture

Per voice:

- Carrier oscillator: analog-style triangle wave.
- Modulator oscillator: phase modulation source.
- Modulator ratio: fixed musical ratios.
- Phase modulation amount: musical amount control.
- Wavefolder: multiple increasing fold stages.
- Wavefolder amount: musical amount control.
- Amp envelope: ADSR.
- Output gain.

### Polyphony

V1 should be polyphonic.

Initial behavior can be conservative:

- Fixed maximum voice count or project setting.
- Voice stealing policy documented and deterministic.
- Per-note voice tracking.
- Release voices continue until envelope release completes.

Suggested v1 voice count:

```text
8 voices or 16 voices
```

The final number can be chosen during implementation based on performance.

### Oscillator Section

Carrier:

- Analog-style triangle.
- Optional subtle shape/tilt parameter if useful.

Modulator:

- Yamaha-style phase modulation oscillator.
- Fixed musical ratios.
- Musical amount control.

Possible ratio values:

```text
0.5x, 1x, 2x, 3x, 4x, 5x, 6x, 8x
```

The exact list can be refined by ear.

### Wavefolder Section

The wavefolder should have multiple stages that increase the amount of folding.

Controls:

- Fold stages: discrete.
- Fold amount: continuous musical amount.

Possible stage values:

```text
1, 2, 3, 4, 5
```

The stage count should be expression-targetable, but since it is discrete it may need smoothing or quantized routing behavior.

### Amp Envelope

Standard ADSR is sufficient for v1.

Controls:

- Attack slider.
- Decay slider.
- Sustain slider.
- Release slider.
- ADSR curve visualization.

The release value should feed the first-party release metadata used later by Expression Mode release ghosts.

### Expression-Ready Parameters

Initial stable parameter destinations:

- `pitch`
- `amp.level`
- `osc.pm.amount`
- `osc.mod.ratio`
- `wavefolder.amount`
- `wavefolder.stages`
- `amp.attack`
- `amp.decay`
- `amp.sustain`
- `amp.release`

Possible additional parameter:

- `osc.triangle.shape`

All parameters should be considered for automation, but not all need to be ideal expression targets. For example, `wavefolder.stages` is useful but discrete.

### Suggested UI Layout

Fixed rack-height horizontal layout:

```text
[Device Header] [Carrier] [Phase Mod] [Wavefolder] [Amp Envelope + Curve] [Output/Voices]
```

Device header:

- Device name: `Simple Osc Complex`
- Bypass/enable.
- Preset/patch name later.

Carrier:

- Triangle indicator.
- Shape/tilt if included.

Phase Mod:

- Ratio selector.
- PM amount slider.

Wavefolder:

- Stage selector or stepped slider.
- Amount slider.
- Small waveform preview if practical later.

Amp Envelope:

- ADSR curve visualization.
- A, D, S, R sliders.

Output/Voices:

- Output level.
- Polyphony indicator or voice count later.

## Device Browser Requirements

The `Devices` browser tab should not depend on plugin scanning.

Device entries should come from the first-party device registry.

Each entry should expose:

- Device type ID.
- Display name.
- Kind.
- Short description.
- Tags/category.
- Default slot creation payload.

Dragging or selecting a first-party device should create a native device slot, not a plugin slot.

Simple Osc Complex should be insertable onto MIDI tracks as an instrument.

Future rules:

- MIDI tracks can host native instruments and MIDI/audio effects according to validation.
- Audio tracks can host native audio effects.
- Return/master tracks can host native audio effects.

## Commands And Undo

First-party device work should use command-backed mutations.

Needed command families:

- Add native device to track.
- Replace device with native device.
- Remove native device.
- Reorder device.
- Set native device bypass.
- Set native device parameter.
- Possibly batch parameter edits for gestures.

Slider gestures should avoid creating excessive undo entries. A likely pattern:

- Begin gesture.
- Preview live value.
- Commit final command at gesture end.

Playback can receive live preview updates during a gesture, but the project model should commit cleanly.

## Serialization And Migration

Project serialization should distinguish native devices from third-party plugins.

Illustrative slot JSON:

```json
{
  "id": "device-1",
  "kind": "instrument",
  "deviceType": "firstParty",
  "firstPartyDevice": {
    "typeId": "tsq.device.instrument.simple-osc-complex",
    "patchVersion": 1,
    "parameters": {
      "osc.pm.amount": 0.35,
      "wavefolder.stages": 2,
      "wavefolder.amount": 0.40,
      "amp.attack": 0.02,
      "amp.decay": 0.18,
      "amp.sustain": 0.75,
      "amp.release": 0.35
    }
  },
  "bypassed": false
}
```

Exact JSON shape should be decided during implementation. The important requirements are stable IDs, versioning, and no opaque native-device blob.

## Testing Strategy

Core tests:

- Device definition registry.
- Patch defaults.
- Patch validation.
- Patch serialization round trip.
- Parameter ID stability.
- Device chain validation with first-party devices.

Engine/integration tests:

- Simple Osc Complex can be inserted on a MIDI track.
- It renders audio for a MIDI note.
- It is polyphonic.
- Amp release metadata matches patch release.
- Parameter changes survive project save/load.
- Parameter changes do not require plugin scanning.

UI tests/probes:

- Device browser shows `Devices` tab.
- Simple Osc Complex appears in the device catalog.
- Device chain fixed-height mode displays inline editor.
- ADSR slider edits update patch state.
- ADSR curve updates when sliders move.

Performance tests:

- Inserting first-party synth does not trigger VST scanning.
- Editing first-party synth parameters does not crawl third-party plugin state.
- Polyphonic note playback remains stable at the chosen v1 voice count.

## Implementation Sequence

Recommended sequence:

1. Add first-party device architecture doc. This document is step 1.
2. Add core first-party device reference/model types.
3. Add first-party device registry with Simple Osc Complex metadata only.
4. Add `Devices` browser tab and first-party device listing.
5. Add native device slot insertion commands.
6. Add device-chain fixed-height mode behavior.
7. Add inline editor host area in device chain.
8. Add Simple Osc Complex patch model and parameter definitions.
9. Add Simple Osc Complex inline editor with inert controls bound to patch state.
10. Add engine-side Simple Osc Complex DSP.
11. Add polyphonic voice allocation and note-ID tracking.
12. Add save/load support for native device patches.
13. Add release metadata path.
14. Add expression capability metadata, even before Expression Mode consumes it.

## Open Design Questions

These do not block the architecture, but should be answered before implementation goes deep:

- Exact fixed device-chain height in pixels.
- Initial voice count: 8, 16, or configurable.
- Exact modulator ratio list.
- Exact wavefolder transfer function.
- Whether `osc.triangle.shape` belongs in v1.
- Whether first-party synth output should initially flow through a Tracktion plugin adapter or a more direct native audio path.
- How parameter slider gestures should be grouped in undo history.
- Whether the device chain should show multiple full first-party editors at once or one full editor plus smaller future strips when chains become crowded.
