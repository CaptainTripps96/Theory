# Expression Mode Implementation Prompt Pack

Created: 2026-06-15

Source spec:
- `/Users/dawneweisman/Downloads/TheorySequencer_Expression_Mode_Spec_v2.txt`
- `docs/TheorySequencer_Expression_Mode_Spec_v2.txt`

This document is a context-window-friendly implementation plan for Expression Mode. It is intentionally split into bounded prompts so the feature can be built over many clean implementation passes without losing the architectural thread.

The core rule for every prompt:

```text
Pristine audio path is king.
Performance is a very close second.
Musical intent stays editable and non-destructive.
```

## Non-Negotiable Constraints

- The core project model remains the source of truth.
- Tracktion Engine remains a playback/plugin/rendering backend, not the product model.
- Core must not depend on UI or Tracktion.
- UI must mutate project state through commands/AppServices, not direct model edits.
- The audio thread must not allocate, lock, scan plugins, parse JSON, log heavily, call UI code, or traverse large project models.
- Expression data is stored as musical objects, not baked MIDI CC, pitch bend, or raw automation.
- Playback/export consumes prepared expression render data built off the audio thread.
- First-party synths should receive semantic expression instructions whenever possible.
- Third-party/plugin/mixer destinations may receive prepared parameter/control streams.
- Every new model type must be serializable, migratable, deterministic, and testable.
- Every dense editing path needs a performance probe before it becomes large.

## Architecture North Star

Expression Mode should introduce three separate layers.

1. Stored musical intent:
   - `ExpressionLane`
   - `ExpressionRoute`
   - `PhraseEnvelopeClip`
   - `CyclicExpressionClip`
   - `PitchSlur`
   - `VibratoExpression`
   - release ghost selection state/display metadata where needed

2. Prepared render representation:
   - immutable or callback-safe objects built during playback sync
   - compact time-ordered events/segments
   - precomputed route mappings
   - no project-model traversal in the audio callback

3. UI/editor projections:
   - Expression Mode toggle
   - left expression menu
   - lane clip strips above piano roll
   - composite overlay curves
   - pitch trajectory overlays
   - release ghost notes
   - expanded edit panels

Do not collapse these layers into one object.

## Recommended Storage Location

Expression objects should live on `MidiClip`, because the spec describes Expression Mode as a piano-roll/clip feature and phrase objects are tied to notes inside a clip.

Recommended high-level shape:

```text
MidiClip
  notes
  harmonicMetadata
  expressionState
    lanes
      volume lane
      pitch lane
      user lanes
```

This keeps copied/moved clips self-contained. Track-level and project-level reuse can be added later through lane presets or linked expression templates.

## Playback Strategy

Expression playback should be prepared during `PlaybackEngine::syncProject`.

For ordinary destinations:

```text
Expression objects
  -> lane composite evaluation
  -> route mapping
  -> prepared destination curves/events
  -> Tracktion/plugin/native parameter application
```

For first-party pitch semantics:

```text
Pitch lane objects
  -> note-ID-aware slur/vibrato instructions
  -> native synth expression event stream
  -> voice-level pitch/no-retrigger behavior
```

V1 can limit playback support while still storing the full model. Never put half-designed realtime logic into the audio callback.

## Performance Budget Direction

These numbers are targets for debug builds where possible; release builds should be comfortably faster.

- Core expression evaluation for a clip with hundreds of notes and expression objects should stay sub-millisecond to low single-digit milliseconds when prepared.
- Piano-roll paint must cull to visible time/lane bounds.
- Composite curves should cache sampled/path geometry per lane revision and viewport scale.
- Dragging/editing should preview locally without resyncing playback for every mouse move.
- Slider/keyboard gestures should commit once per gesture where practical.
- Playback sync should reuse unchanged prepared expression render data.
- Dense LFO/composite rendering should produce compact segment/event streams, not per-sample project-model lookups.

## Cross-Cutting Test Requirements

Every implementation prompt that changes behavior should add or update tests. Prefer:

- Core unit tests for model invariants and evaluators.
- Serialization tests for every stored object.
- Command tests for undo/redo.
- Integration tests for AppServices/playback sync.
- UI performance probes for paint and interaction hot paths.
- Audio/render tests for first-party synth expression events.

Use performance probes early. Do not wait until the feature feels slow.

## Prompt Pack Driver Instructions

Each future prompt should start by reading:

- `docs/EXPRESSION_MODE_IMPLEMENTATION_PROMPT_PACK.md`
- `docs/TheorySequencer_Expression_Mode_Spec_v2.txt`
- `docs/agent/02_ARCHITECTURE_GUARDRAILS.txt`
- The specific files named in that prompt

Each future prompt should end by updating this document or a companion handoff note with:

- what changed
- tests run
- performance observations
- next prompt to run
- any deferred risks

Do not combine prompts unless the previous prompt explicitly completed with plenty of context budget remaining.

---

# Prompt 00 - Baseline Audit And Test Harness

## Goal

Create a factual baseline before Expression Mode code starts. Identify current piano-roll paint costs, command costs, playback sync costs, and native synth render behavior so future prompts can prove they did not damage responsiveness or audio quality.

## Read

- `docs/DEBUGGING.md`
- `docs/SYSTEM_PERFORMANCE_AUDIT_PLAN.md`
- `docs/performance-audit/04_PIANO_ROLL_RENDERING_LANES_AND_NOTE_EDITING.md`
- `docs/performance-audit/09_TRACKTION_ENGINE_SYNC_AND_PLAYBACK_GRAPH_MATERIALIZATION.md`
- `docs/performance-audit/10_AUTOMATION_EDITING_AND_PLAYBACK_BINDING.md`
- `src/ui/PianoRollComponent.cpp`
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/core/devices/SimpleOscComplexSynth.cpp`
- `tests/integration/*PerformanceProbeTest.cpp`

## Implement

- Add an Expression Mode baseline/performance probe file without implementing Expression Mode.
- Measure current piano-roll paint with small, medium, and dense MIDI clips.
- Measure note selection/marquee selection on dense clips.
- Measure playback sync with Simple Osc Complex on a MIDI track.
- Add audio render sanity checks for Simple Osc Complex if not already present:
  - finite output
  - no NaN/inf
  - bounded peak
  - deterministic output for same MIDI/patch input

## Tests

- Build `tsq_engine_integration_tests`.
- Run the new baseline probe with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`.
- Run Simple Osc Complex core tests.

## Non-Goals

- Do not add Expression Mode UI.
- Do not change project serialization.
- Do not alter audio behavior except for test-only harnesses.

## Done When

- There is a repeatable baseline command.
- Results are saved under `docs/performance-audit/`.
- Later prompts have known numbers to beat or preserve.

---

# Prompt 01 - Core Expression Domain Types

## Goal

Add pure core model types for Expression Mode without UI, commands, serialization, or playback integration.

## Read

- `docs/TheorySequencer_Expression_Mode_Spec_v2.txt`
- `src/core/sequencing/MidiClip.h`
- `src/core/sequencing/MidiClip.cpp`
- `src/core/sequencing/Automation.h`
- `src/core/time/GridDivision.h`
- `src/core/time/ProjectRhythmSettings.h`
- `src/core/sequencing/DeviceChain.h`

## Implement

Create a new core module, likely:

```text
src/core/sequencing/Expression.h
src/core/sequencing/Expression.cpp
```

Add model types:

- `ExpressionLaneId`
- `ExpressionClipId`
- `ExpressionRouteId`
- `ExpressionLanePolarity`
- `ExpressionCurveShape`
- `ExpressionLane`
- `ExpressionRoute`
- `ExpressionDestination`
- `PhraseEnvelopeClip`
- `EnvelopeStage`
- `CyclicExpressionClip`
- `CyclicWaveShape`
- `CyclicBlendMode`
- `CyclicWavePolarityMode`
- `PitchSlur`
- `VibratoExpression`
- block IDs and override structs for later slur/vibrato sharing
- `ExpressionState` owned by `MidiClip`

Important invariants:

- Default lanes exist: Volume unipolar and Pitch bipolar.
- Lane IDs are stable and unique within a clip.
- Clip/object IDs are stable and unique within their lane/type.
- Unipolar values clamp to `0.0..1.0`.
- Bipolar values clamp to `-1.0..1.0`.
- Destination mappings allow inversion by `outputMax < outputMin`.
- Envelope stage durations use `TickDuration`.
- `attack + decay + release <= phraseLength`.
- Cyclic clips cannot overlap within the same lane/time span.
- Pitch objects reference note IDs, not only pitches.

## Tests

Add `tests/core/ExpressionModelTest.cpp`.

Cover:

- default lane construction
- lane add/rename/remove
- route add/remove/mapping inversion
- polarity value clamping
- envelope timing validation
- cyclic overlap rejection
- slur and vibrato object construction
- `MidiClip` expression state copy/replace behavior

## Non-Goals

- No serialization.
- No commands.
- No UI.
- No playback rendering.

## Audio/Performance Notes

Keep these types simple value types. Do not introduce virtual dispatch or heap-heavy graph structures in the core model unless there is a clear measured reason.

## Prompt 01 Status - Implemented 2026-06-15

Changed:

- Added pure core expression model files:
  - `src/core/sequencing/Expression.h`
  - `src/core/sequencing/Expression.cpp`
- Added `ExpressionState` ownership to `MidiClip`.
- Added CMake wiring for the new core source and test file.
- Added `tests/core/ExpressionModelTest.cpp`.

Covered:

- Default Volume/Pitch lanes.
- Lane add/rename/remove behavior.
- Route add/remove, destination validation, and inverted range mapping.
- Unipolar/bipolar clamping.
- Phrase envelope timing validation.
- Cyclic overlap rejection.
- Pitch slur and vibrato model validation.
- `MidiClip` expression state copy/replace behavior.

Tests run:

```text
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "*Expression*"
./build/tests/tsq_core_tests
```

Result:

```text
All tests passed (5283 assertions in 200 test cases)
```

Performance observations:

- This pass only adds value-model storage and validation.
- No UI, playback, serialization, command, or audio-thread work was added.
- No virtual dispatch or realtime graph logic was introduced.

Deferred risks:

- Prompt 02 must define stable JSON enum strings and round-trip behavior.
- Prompt 03 should decide whether expression mutations live in new command files or near existing clip mutation commands.
- Later playback prompts must keep prepared render data separate from these editable model objects.

Next prompt:

- Prompt 02 - Expression Serialization And Migration

---

# Prompt 02 - Expression Serialization And Migration

## Goal

Persist Expression Mode data in `.tseq` projects with versioned, deterministic JSON.

## Read

- `src/core/serialization/ProjectSerializer.cpp`
- `src/core/serialization/ProjectMigration.cpp`
- `tests/core/ProjectSerializationTest.cpp`
- `docs/agent/02_ARCHITECTURE_GUARDRAILS.txt`
- Prompt 01 implementation

## Implement

- Serialize `MidiClip.expressionState`.
- Deserialize missing expression state by creating default Volume and Pitch lanes.
- Add schema migration if project schema version changes.
- Use stable string enum values.
- Preserve unknown future-safe fields only if current serializer pattern supports it; otherwise document that unknown expression fields are rejected.
- Keep JSON deterministic:
  - stable object ordering
  - stable IDs
  - no floating formatting surprises beyond existing serializer behavior

Suggested JSON direction:

```json
"expression": {
  "lanes": [
    {
      "id": "expr-volume",
      "name": "Volume",
      "polarity": "unipolar",
      "enabled": true,
      "phraseEnvelopes": [],
      "cyclic": [],
      "routes": []
    }
  ]
}
```

## Tests

Add serialization round trips for:

- default expression state
- renamed user lane
- route mappings including inverted min/max
- phrase envelope with attack/decay/release
- cyclic clip
- slur block
- vibrato expression with override placeholder
- backward compatibility for projects without expression JSON

## Non-Goals

- No UI.
- No playback.
- No command layer.

## Done When

- Saving/loading a project with expression objects round-trips exactly enough for model equality.
- Old projects load with default lanes.

## Prompt 02 Status - Implemented 2026-06-15

Changed:

- Added deterministic expression JSON serialization/deserialization in `src/core/serialization/ProjectSerializer.cpp`.
- Serialized `MidiClip.expressionState` under each MIDI clip as `"expression"`.
- Added stable string enum encodings for:
  - lane polarity
  - curve shape
  - destination kind
  - envelope stage type
  - cyclic wave shape/blend/polarity modes
- Added round-trip support for:
  - lanes and enabled state
  - routes with inverted output ranges
  - phrase envelopes with attack/decay/release, peak/sustain, and tail extension
  - cyclic clips
  - pitch slurs with block IDs
  - vibrato expressions with voice overrides
- Added backward compatibility for clips without `"expression"` JSON by preserving `MidiClip`'s default expression state.
- Did not bump `currentProjectSchemaVersion`; missing expression data is handled as an additive v2 field.

Tests run:

```text
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "*expression*"
./build/tests/tsq_core_tests
```

Result:

```text
All tests passed (5332 assertions in 202 test cases)
```

Performance observations:

- Serialization remains object/array based and deterministic through the existing `JsonValue::Object` map ordering.
- No playback, UI, or audio-thread work was added.
- Expression deserialization validates through the core model constructors and lane add methods.

Deferred risks:

- Unknown future expression fields are currently ignored only at the object-field level if the current reader never asks for them; unknown enum values are rejected.
- Prompt 03 should add command-level mutation paths instead of letting UI mutate expression state directly.
- Later prepared-render prompts should not consume serializer objects directly on the audio thread.

Next prompt:

- Prompt 03 - Expression Commands And AppServices API

---

# Prompt 03 - Expression Commands And AppServices API

## Goal

Add command-backed mutations and AppServices methods for Expression Mode.

## Read

- `src/core/commands/Command.h`
- `src/core/commands/MixerCommands.h`
- `src/core/commands/MixerCommands.cpp`
- `src/core/commands/ProjectMutationCommands.cpp`
- `src/app/AppServices.h`
- `src/app/AppServices.cpp`
- `tests/core/MixerCommandTest.cpp`

## Implement

Add focused commands:

- create expression lane
- rename expression lane
- set lane enabled
- set lane polarity
- add/remove expression route
- set route mapping
- add/delete phrase envelope
- edit phrase envelope stage duration/level/curve
- add/delete cyclic clip
- edit cyclic clip parameters
- add/delete slur
- edit slur block/voice settings
- add/delete vibrato
- edit vibrato block/voice settings

Add AppServices wrappers.

Dirty category:

- Add a playback dirty category for expression changes.
- Changes that affect only UI display state should not require playback sync.
- Changes that affect rendered expression should require playback sync or a future lightweight expression-sync path.

Undo behavior:

- One command per intentional keyboard step.
- For continuous slider gestures, prefer preview plus final commit command.

## Tests

Add command tests for:

- execute/undo/redo
- validation failure on missing clip/lane/object
- phrase timing constraints
- route mapping
- slur/vibrato block override edits
- dirty category correctness

## Non-Goals

- No piano-roll UI.
- No playback renderer.
- No serialization beyond using Prompt 02 behavior.

## Prompt 03 Status - Implemented 2026-06-15

Changed:

- Added `PlaybackSyncCategory::expression` and made it playback-relevant for dirty tracking.
- Added expression command module:
  - `src/core/commands/ExpressionCommands.h`
  - `src/core/commands/ExpressionCommands.cpp`
- Added command-backed clip expression mutations for:
  - replacing full clip expression state
  - creating/renaming/enabling/polarity-changing lanes
  - adding/removing routes
  - adding/removing phrase envelopes
  - adding/removing cyclic clips
  - adding/removing pitch slurs
  - adding/removing vibrato expressions
- Added AppServices wrappers for those command-backed expression mutations.
- Added command tests in `tests/core/CommandStackTest.cpp`.
- Added an AppServices integration smoke test for expression lane creation and undo.

Tests run:

```text
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "*Expression*"
./build/tests/tsq_core_tests
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "[expression]"
```

Result:

```text
Core: all tests passed (5375 assertions in 205 test cases)
Integration expression filter: all tests passed (32 assertions in 3 test cases)
```

Performance observations:

- Commands copy and replace `ExpressionState` at clip scope. This is simple and safe for early UI work, but dense-expression editing may eventually need more targeted commands or move-aware mutation paths.
- Dirty marking is category-based through the existing command stack callback.
- No playback renderer or audio-thread code was added.

Deferred risks:

- Fine-grained edit commands for envelope stage parameters, cyclic parameters, and slur/vibrato override fields are still deferred. Current AppServices can replace the full state or add/remove objects, which is enough to wire initial UI workflows.
- Prompt 04 should add pure evaluation without involving AppServices or Tracktion.
- Later UI prompts should avoid continuous command spam during slider drags; preview locally and commit once per gesture.

Next prompt:

- Prompt 04 - Expression Evaluator And Composite Curve Engine

---

# Prompt 04 - Expression Evaluator And Composite Curve Engine

## Goal

Implement pure core evaluation of expression lanes into composite normalized values over time.

## Read

- Prompt 01 model
- `src/core/sequencing/Automation.cpp`
- `src/core/sequencing/AutomationPlayback.cpp`
- `src/core/time/GridDivision.cpp`
- `tests/integration/AutomationPerformanceProbeTest.cpp`

## Implement

Create:

```text
src/core/sequencing/ExpressionEvaluation.h
src/core/sequencing/ExpressionEvaluation.cpp
```

Responsibilities:

- evaluate phrase envelope value at a local clip tick
- evaluate cyclic expression at a local clip tick
- combine phrase and cyclic values
- apply additive and multiplicative cyclic blend modes
- clamp/normalize only spans that overflow lane bounds
- provide sampled curves for UI rendering
- provide compact segments/events for playback preparation

Curve shapes:

- linear
- logarithmic
- exponential

LFO shapes:

- sine
- triangle
- ramp up
- ramp down
- square

Important:

- Use ticks and doubles.
- Do not allocate per sample.
- Provide APIs that allow caller-owned output vectors.
- Evaluation must be deterministic.

## Tests

Add `tests/core/ExpressionEvaluationTest.cpp`.

Cover:

- linear/log/exponential envelope stages
- attack-only envelope
- attack/release envelope
- full attack/decay/sustain/release envelope
- stored level behavior
- additive cyclic blend
- multiplicative cyclic blend
- positive oscillator mode
- half-wave rectified mode
- local overflow normalization span
- bipolar lane evaluation
- sampled curve generation

## Performance Probe

Add or extend an integration/core performance probe:

- 1 lane, 10 clips
- 8 lanes, 100 clips
- dense cyclic sampling across long clips
- repeated viewport sampling

## Non-Goals

- No UI drawing.
- No routing to actual playback destinations.
- No pitch slur/vibrato trajectory yet beyond generic evaluator pieces.

## Prompt 04 Status - Implemented 2026-06-15

Changed:

- Added pure core expression evaluator files:
  - `src/core/sequencing/ExpressionEvaluation.h`
  - `src/core/sequencing/ExpressionEvaluation.cpp`
- Added deterministic evaluation for:
  - phrase envelope stages
  - linear/logarithmic/exponential curves
  - cyclic sine/triangle/ramp-up/ramp-down/square waves
  - positive oscillator and half-wave rectified cyclic polarity modes
  - additive and multiplicative cyclic blends
  - unipolar/bipolar lane bounds
- Added caller-owned output APIs for:
  - sampled expression curves
  - compact linear expression segments
- Added sampled-span overflow normalization for curves that exceed lane bounds.
- Added `tests/core/ExpressionEvaluationTest.cpp`.
- Added a lightweight core performance probe covering 8 lanes, 100 adjacent cyclic clips per lane, and repeated viewport sampling.

Semantics:

- A lane's neutral value is `0.0` for both unipolar and bipolar lanes.
- Phrase envelopes are the active base value when their region/tail contains the queried tick.
- If multiple phrase envelopes overlap, later lane order wins.
- Cyclic clips modulate the current lane value.
- Scalar lane evaluation clamps to lane bounds when normalization is enabled.
- Sampled lane evaluation normalizes the sampled span when it overflows, preserving the curve shape across the requested viewport.

Tests run:

```text
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "*Expression*"
./build/tests/tsq_core_tests
```

Result:

```text
Expression filter: all tests passed (171 assertions in 17 test cases)
Core: all tests passed (5419 assertions in 213 test cases)
```

Performance observations:

- Point evaluation performs no per-sample allocation.
- Sampling writes to caller-owned output vectors.
- `buildExpressionSegments` currently uses a local scratch sample vector internally; callers that need stricter allocation control can be given a scratch-vector overload in a later prompt.
- The current lane evaluator scans lane objects linearly. Prompt 08/09 playback preparation should cache prepared expression data for dense projects rather than evaluating editable model objects directly.

Deferred risks:

- Pitch slur/vibrato trajectory semantics are intentionally deferred.
- Segment simplification is currently one segment per sample interval; later playback prep can merge flat or collinear spans.
- The exact multiplicative blend semantics may need UI/audio review once expression curves are visible and audible.

Next prompt:

- Prompt 05 - Expression Destination Registry And Route Mapping

---

# Prompt 05 - Expression Destination Registry And Route Mapping

## Goal

Define route destinations in a stable, testable way so expression lanes can target mixer, track, plugin, MIDI CC, and first-party device parameters without hard-coding UI strings.

## Read

- `src/core/sequencing/Automation.h`
- `src/core/sequencing/DeviceChain.h`
- `src/core/devices/FirstPartyDeviceRegistry.h`
- `src/core/devices/FirstPartyDeviceRegistry.cpp`
- `src/ui/DeviceChainComponent.cpp`
- Prompt 01 model

## Implement

Add a core destination model:

- destination kind
- track ID
- device slot ID
- parameter ID
- MIDI CC number
- pitch bend destination
- mixer destination
- stable display metadata helper where appropriate

Add route mapping helpers:

- unipolar lane to arbitrary destination min/max
- bipolar lane to arbitrary destination min/max
- inversion support
- optional mapping curve placeholder
- optional smoothing policy placeholder

Add first-party destination metadata:

- expression-targetable flag already exists conceptually in first-party parameters; make sure it can be queried.
- Simple Osc Complex should expose stable destination IDs for its parameters.

## Tests

Cover:

- stable destination IDs
- mapping math
- inversion
- first-party parameter lookup
- invalid destination rejection
- route equality and serialization compatibility

## Non-Goals

- No playback application yet.
- No UI route picker yet.

## Prompt 05 Status - Implemented 2026-06-15

Changed:

- Added first-party device registry helpers:
  - `findFirstPartyParameterDefinition`
  - `expressionTargetParameters`
- Added expression destination registry files:
  - `src/core/sequencing/ExpressionDestinationRegistry.h`
  - `src/core/sequencing/ExpressionDestinationRegistry.cpp`
- Added stable destination kind string helpers.
- Added route mapping helpers:
  - unipolar and bipolar lane mapping
  - inverted output range support
  - mapping curve placeholder (`ExpressionRouteMappingCurve::linear`)
  - smoothing placeholder (`ExpressionRouteSmoothingPolicy::none`)
- Added destination availability checks against a `Project`.
- Added metadata lookup for:
  - track volume
  - track pan
  - pitch
  - pitch bend
  - send level
  - MIDI CC
  - plugin parameter placeholders
  - first-party parameters with actual parameter definition metadata
- Added track destination enumeration for core/UI route picker use.
- Added `tests/core/ExpressionDestinationRegistryTest.cpp`.

Tests run:

```text
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "*Expression*"
./build/tests/tsq_core_tests
```

Result:

```text
Expression filter: all tests passed (217 assertions in 24 test cases)
Core: all tests passed (5465 assertions in 220 test cases)
```

Performance observations:

- Destination lookup is project-model traversal and is intended for UI/edit/preparation paths, not audio-thread use.
- First-party parameter metadata is static and cheap to query.
- Route value mapping delegates to `ExpressionRoute::mapLaneValue` and performs no allocation.

Deferred risks:

- Plugin parameter metadata is still a placeholder because plugin parameter discovery/metadata is engine-facing and should be integrated carefully in a later prompt.
- Destination enumeration currently includes first-party parameter destinations but does not enumerate arbitrary MIDI CC ranges or plugin parameter lists.
- Prompt 06 should consume destination metadata to build prepared route data off the audio thread.

Next prompt:

- Prompt 06 - Prepared Expression Render Model

---

# Prompt 06 - Prepared Expression Render Model

## Goal

Introduce an engine-facing prepared expression representation that is built off the audio thread and can be consumed without project traversal.

## Read

- `src/engine/PlaybackEngine.h`
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/engine/devices/SimpleOscComplexTracktionPlugin.cpp`
- `docs/performance-audit/09_TRACKTION_ENGINE_SYNC_AND_PLAYBACK_GRAPH_MATERIALIZATION.md`
- Prompt 04 evaluator
- Prompt 05 destination model

## Implement

Create an engine/core boundary representation, likely in `src/core/sequencing` or `src/engine` depending on dependency direction:

- prepared lane render data
- prepared route render data
- destination event/segment streams
- pitch semantic event streams for first-party synths
- revision/fingerprint support for reuse

Rules:

- Build during playback sync or an explicit preparation phase.
- Store per clip/track/device destination.
- Avoid storing per-sample values unless absolutely necessary.
- Prefer piecewise linear/curve segments or sparse events with interpolation.
- Provide deterministic equality/fingerprint for reuse.

## Tests

Core tests:

- prepared render from expression lane
- route mapping output
- stable fingerprints when model unchanged
- changed fingerprint when expression object changes

Integration/perf:

- sync project with expression lanes and verify preparation time
- unchanged expression sync reuses prepared render data where possible

## Non-Goals

- Do not apply the data to synth/plugin parameters yet.
- Do not add UI.

## Prompt 06 Status - Implemented 2026-06-15

Implemented a core prepared expression render model that can be built off the audio thread and consumed without traversing `Project`.

Files added:

- `src/core/sequencing/PreparedExpressionRenderModel.h`
- `src/core/sequencing/PreparedExpressionRenderModel.cpp`
- `tests/core/PreparedExpressionRenderModelTest.cpp`

Build wiring:

- Added `sequencing/PreparedExpressionRenderModel.cpp` to `src/core/CMakeLists.txt`.
- Added `core/PreparedExpressionRenderModelTest.cpp` to `tests/CMakeLists.txt`.

Behavior added:

- Prepared clip render data contains track ID, clip ID, clip project start, local clip region, and prepared lanes.
- Prepared lanes contain sampled lane segments, prepared route streams, pitch slur events, vibrato events, and deterministic fingerprints.
- Prepared route streams contain destination data, stable destination IDs, output ranges, smoothing policy placeholders, availability state, mapped output segments, and deterministic fingerprints.
- Unavailable routes remain addressable in the prepared model but do not produce output segments.
- Route output segments are mapped from lane segments through the route output range during preparation.
- Pitch semantic data is copied into sparse prepared events for future first-party synth consumption.
- Prepared model fingerprints remain stable for unchanged expression data and change when render-relevant route data changes.

Verification:

```bash
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "*Expression*"
./build/tests/tsq_core_tests
```

Results:

```text
Expression filter: all tests passed (261 assertions in 28 test cases)
Core: all tests passed (5509 assertions in 224 test cases)
```

Carry forward:

- Prompt 07 can treat `PreparedExpressionRenderModel` as the non-UI prepared representation, but should not use it for ordinary UI painting unless needed.
- Playback integration still needs an engine-side cache/reuse layer keyed by prepared fingerprints.
- Applying prepared route streams to first-party devices, plugins, MIDI CC, sends, or track mixer targets remains intentionally deferred.

---

# Prompt 07 - Expression Mode UI Shell

## Goal

Add Expression Mode as a visible piano-roll mode with no deep editing yet.

## Read

- `src/ui/PianoRollComponent.h`
- `src/ui/PianoRollComponent.cpp`
- `src/app/AppServices.h`
- `src/app/AppServices.cpp`
- Prompt 03 AppServices APIs

## Implement

UI features:

- Button/menu left of piano roll to enter/exit Expression Mode.
- Expression lane list.
- Default Volume and Pitch lanes visible.
- Plus button to create a lane.
- Lane rename control.
- Lane enable/disable.
- Polarity selector.
- Selected lane state in the UI.

Mode behavior:

- MIDI notes remain visible.
- Pitch editing is disabled in Expression Mode.
- Notes can still be selected.
- Marquee selection still selects notes.
- Expression Mode has its own key-command branch.
- Ordinary piano-roll note editing shortcuts should not leak into Expression Mode.

## Tests

Add UI/integration test or probe where possible:

- opening clip shows default lanes
- toggling Expression Mode does not mutate notes
- creating/renaming lane uses commands
- selected lane survives refresh

## Non-Goals

- No expression clips.
- No overlay curves.
- No playback.
- No route picker yet if too large; leave placeholder.

## Performance Notes

Do not trigger playback sync when only toggling Expression Mode or changing selected lane display.

## Prompt 07 Status - Implemented 2026-06-15

Implemented the first visible Expression Mode UI shell in the piano roll.

Files changed:

- `src/ui/PianoRollComponent.h`
- `src/ui/PianoRollComponent.cpp`
- `tests/CMakeLists.txt`

Files added:

- `tests/integration/ExpressionModeUiShellTest.cpp`

Behavior added:

- Added an `Expression` toggle button in the piano-roll toolbar.
- Added a fixed expression lane panel to the left of the piano-roll viewport while Expression Mode is enabled.
- The lane panel shows the clip's default Volume and Pitch lanes plus any user-created lanes.
- Added selected expression lane state, with automatic fallback to a valid lane when clips are opened/refreshed.
- Added a plus button for command-backed lane creation.
- Added selected-lane rename, enable/disable, and polarity controls backed by `AppServices` expression command methods.
- Added UI/debug helpers for expression-mode tests.
- MIDI notes remain visible and selectable in Expression Mode.
- Marquee selection and `Command+A` note selection continue to work in Expression Mode.
- Ordinary note mutation paths are guarded in Expression Mode: double-click create/delete, drag move/resize, delete, paste, duplicate, arrow movement, scalar fill, arpeggiation, chord stacking, and chromatic toggle shortcuts do not leak through.

Verification:

```bash
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*Expression Mode UI shell*"
./build/tests/tsq_core_tests
./build/tests/tsq_engine_integration_tests "*Expression*"
```

Results:

```text
Expression Mode UI shell: all tests passed (29 assertions in 2 test cases)
Core: all tests passed (5509 assertions in 224 test cases)
Expression integration filter: all tests passed (61 assertions in 5 test cases)
```

Notes:

- Headless integration runs still print existing JUCE assertion messages during plugin-format/audio-device initialization and component painting, but the tests exit successfully.
- Prompt 08 should build on the mode/sidebar state and add overlay rendering/clip strips without moving expression-mode state into playback sync.

---

# Prompt 08 - Expression Overlay Rendering And Lane Clip Strips

## Goal

Render expression overlays in the piano roll for selected lane data.

## Read

- `src/ui/PianoRollComponent.cpp`
- Prompt 04 evaluator
- Prompt 07 UI shell
- `docs/performance-audit/04_PIANO_ROLL_RENDERING_LANES_AND_NOTE_EDITING.md`

## Implement

Render:

- faded MIDI notes while Expression Mode is active
- phrase envelope clip strip above piano roll
- cyclic/LFO clip strip above piano roll
- selected lane composite curve over notes
- unipolar floor/ceiling guide lines
- bipolar floor/center/ceiling guide lines
- bound warning opacity as values approach limits

For phrase envelope clips:

- collapsed clips show stage partitioning
- attack/decay-sustain/release have stable palette-appropriate phase colors
- expanded controls later should reuse those colors

Performance:

- cull to visible tick range
- cache sampled/path geometry by clip revision, lane ID, viewport scale, and visible range bucket
- avoid evaluating every expression lane every paint
- evaluate/draw only selected lane plus cheap summaries for list/strip

## Tests

Add a performance probe:

- paint no Expression Mode
- paint Expression Mode with empty lanes
- paint with 10 phrase envelopes
- paint with dense cyclic curve
- ensure no large regression against Prompt 00 baseline

## Non-Goals

- No keyboard editing.
- No route menu.
- No playback.

## Prompt 08 Status - Implemented 2026-06-15

Implemented the first Expression Mode overlay rendering pass in the piano roll.

Files changed:

- `src/ui/PianoRollComponent.cpp`
- `tests/integration/ExpressionModeUiShellTest.cpp`

Behavior added:

- MIDI notes are visually faded while Expression Mode is active.
- The selected expression lane draws an over-note composite curve.
- Unipolar lanes draw floor/ceiling guide lines.
- Bipolar lanes draw floor/center/ceiling guide lines.
- Values near lane limits draw subtle warning accents.
- Phrase envelope clips draw collapsed strips in the ruler band.
- Phrase envelope strips show attack and release/stage partition markers.
- Cyclic/LFO clips draw collapsed strips in the ruler band.
- Overlay evaluation is limited to the selected lane.
- Overlay sampling is culled to visible layout spans.
- Overlay path/strip geometry is cached by selected lane content, viewport bucket, zoom, row height, and visible value area.

Tests/perf probes:

- Extended `ExpressionModeUiShellTest.cpp` with an overlay paint probe that covers:
  - no Expression Mode
  - Expression Mode with empty lanes
  - Expression Mode with 10 phrase envelopes
  - Expression Mode with a dense sixteenth-note cyclic curve
  - cold and warm dense cyclic paints

Verification:

```bash
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_core_tests
./build/tests/tsq_engine_integration_tests "*Expression*"
```

Results:

```text
Expression Mode focused tests: all tests passed (62 assertions in 5 test cases)
Core: all tests passed (5509 assertions in 224 test cases)
Expression integration filter: all tests passed (70 assertions in 6 test cases)
```

Notes:

- Headless integration runs still print existing JUCE assertion messages during plugin-format/audio-device initialization and component painting, but the tests exit successfully.
- Prompt 09 can build phrase selection and release ghost notes on top of the strips/selected-lane overlay introduced here.

---

# Prompt 09 - Phrase Selection And Release Ghost Notes

## Goal

Add Expression Mode phrase selection semantics and release ghost note display/selection for first-party synth release metadata.

## Read

- Spec sections 23 and 54
- `src/ui/PianoRollComponent.cpp`
- `src/core/devices/FirstPartyDeviceRegistry.*`
- `src/core/devices/SimpleOscComplexSynth.*`
- `src/engine/devices/SimpleOscComplexTracktionPlugin.*`
- `src/app/AppServices.*`

## Implement

Core/helper behavior:

- derive release duration from active first-party synth patch
- convert release seconds to ticks using project tempo at note end
- compute ghost bounds per note
- Release Mode toggle in Expression Mode UI

UI behavior:

- ghost note extension appears only in Expression Mode when Release Mode is enabled
- ghost is low-opacity and visually secondary
- ghost does not mutate MIDI note duration
- marquee selection can include ghost tails when Release Mode is enabled
- expression phrase bounds can extend through selected ghost tails

## Tests

Core tests:

- release seconds to ticks at tempo
- ghost phrase bound computation

UI/integration:

- release ghost hidden by default
- release ghost visible in Release Mode
- marquee selection includes/excludes ghost based on toggle

## Non-Goals

- No release-tail expression playback yet.
- No dynamic modulation of release metadata.

## Prompt 09 Status - Implemented 2026-06-15

Implemented first-party release ghost helpers and opt-in release-tail display/selection in Expression Mode.

Files added:

- `src/core/sequencing/ExpressionReleaseGhosts.h`
- `src/core/sequencing/ExpressionReleaseGhosts.cpp`
- `tests/core/ExpressionReleaseGhostsTest.cpp`

Files changed:

- `src/core/CMakeLists.txt`
- `src/ui/PianoRollComponent.h`
- `src/ui/PianoRollComponent.cpp`
- `tests/CMakeLists.txt`
- `tests/integration/ExpressionModeUiShellTest.cpp`

Core behavior added:

- Converts release seconds to ticks using `TempoMap` at the note end.
- Resolves Simple Osc Complex `amp.release` from first-party device state through the existing synth patch mapping.
- Computes per-note release ghost regions and phrase regions without mutating MIDI note duration.

UI behavior added:

- Added an Expression Mode `Release Mode` toggle in the expression lane panel.
- Release ghosts are hidden by default.
- Release ghosts draw only when Expression Mode and Release Mode are both enabled.
- Ghost tails are low-opacity and visually secondary.
- Marquee selection can include ghost tails only when Release Mode is enabled.
- Added debug/test hook for marquee-selecting the first release ghost.

Verification:

```bash
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_core_tests "*Release*"
./build/tests/tsq_engine_integration_tests "*release ghosts*"
./build/tests/tsq_core_tests
./build/tests/tsq_engine_integration_tests "*Expression*"
```

Results:

```text
Release-focused core tests: all tests passed (35 assertions in 5 test cases)
Release ghost integration: all tests passed (7 assertions in 1 test case)
Core: all tests passed (5523 assertions in 226 test cases)
Expression integration filter: all tests passed (77 assertions in 7 test cases)
```

Notes:

- The current UI draws release ghosts inside the existing clip viewport. It does not expand piano-roll content width beyond the clip end yet.
- Release ghosts are visual/selection helpers only; they do not affect playback or expression evaluation.
- Headless integration runs still print existing JUCE assertion messages during plugin-format/audio-device initialization and component painting, but the tests exit successfully.

---

# Prompt 10 - Phrase Envelope Creation And Keyboard Editing

## Goal

Implement the phrase envelope workflow from the spec.

## Read

- Spec sections 12-23 and 53
- `src/ui/PianoRollComponent.cpp`
- Prompt 03 commands
- Prompt 04 evaluator
- Prompt 08 overlay rendering
- Prompt 09 phrase selection

## Implement

Keyboard behavior in Expression Mode:

- `A + Right` creates/lengthens attack.
- `A + Left` shortens attack.
- `A + Up/Down` changes attack start level.
- `D + Right` creates/lengthens decay.
- `D + Left` shortens decay.
- `D + Up/Down` changes sustain level.
- `R + Left` creates/lengthens release backward.
- `R + Right` shortens release.
- `R + Up/Down` changes release end level.
- `F + Up/Down` changes peak/force when decay exists.
- `C + Up/Down` cycles selected/active envelope segment curve.
- Delete removes selected envelope.

Behavior:

- `A` alone creates nothing.
- Initial envelope captures stored level from current lane value.
- Timing uses project rhythm grid/arpeggio subdivision logic.
- `attack + decay + release <= phraseLength`.
- Stage colors update in collapsed and expanded display.

## Tests

Core command tests:

- create attack from selection
- all keyboard edits execute/undo
- timing cannot exceed phrase length
- stored level capture
- unipolar/bipolar default levels
- curve cycling

UI/integration:

- key presses create/update visible phrase envelope
- ordinary note pitch edits disabled in Expression Mode

## Performance Notes

Keyboard edits should update one object and one lane cache, not rebuild all expression data for all clips.

## Implementation Status

Implemented Prompt 10 phrase-envelope keyboard creation and editing.

Code added:

- Added `src/core/sequencing/ExpressionPhraseEditing.h/.cpp`.
- Added `tests/core/ExpressionPhraseEditingTest.cpp`.
- Wired the piano roll Expression Mode key branch to phrase-envelope editing.
- Added `PianoRollComponent::debugExpressionKeyPress` for deterministic integration coverage.

Behavior added:

- `A` alone only arms attack editing.
- `A + Right` creates an attack phrase envelope from the selected notes.
- `A + Left/Right/Up/Down`, `D + Left/Right/Up/Down`, `R + Left/Right/Up/Down`, `F + Up/Down`, and `C + Up/Down` edit the active phrase envelope.
- Delete removes the selected active phrase envelope.
- Stored level is captured from the current selected expression lane at the phrase start.
- Timing is grid-based and constrained so attack + decay + release never exceeds the phrase length.
- Plain arrows remain consumed in Expression Mode and do not mutate MIDI note pitch/start.
- Overlay cache fingerprinting now includes stage curves, levels, peak, sustain, and release details so envelope edits repaint immediately.

Validation:

```sh
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "*Phrase envelope*"
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_core_tests
```

Results:

- Phrase envelope focused core tests: all tests passed, 64 assertions in 6 test cases.
- Expression Mode focused integration tests: all tests passed, 99 assertions in 7 test cases.
- Full core suite: all tests passed, 5566 assertions in 229 test cases.

---

# Prompt 11 - Expanded Phrase Envelope Controls

## Goal

Add the expanded phrase envelope menu/control surface.

## Read

- Spec sections 22, 48.2, 53
- `src/ui/PianoRollComponent.cpp`
- Prompt 10 implementation
- Existing device-chain slider gesture handling in `src/ui/DeviceChainComponent.cpp`

## Implement

When a phrase envelope clip or segment is selected:

- expand lane vertically
- show attack time/start/curve controls
- show decay time/peak/sustain/curve controls when present
- show release time/end/curve controls when present
- show stored level readout
- show optional tail/release extension behind context menu or hidden advanced control
- use the same phase colors as collapsed partitions

Gesture behavior:

- preview continuously
- commit final command at gesture end
- avoid a new undo entry for every slider pixel

## Tests

- selecting clip expands controls
- controls reflect model
- slider gesture commits once
- undo restores previous values
- hidden absent stages do not show misleading controls

## Non-Goals

- No cyclic controls.
- No route picker.

## Implementation Status

Implemented Prompt 11 expanded phrase-envelope controls.

Code added:

- Added a `PhraseEnvelopeControlPanel` inside `PianoRollComponent`.
- Lifted active phrase-envelope selection to the piano-roll owner so overlay selection and the control panel share state.
- Added phrase-envelope strip IDs and selected-strip highlighting in the overlay.
- Added deterministic debug hooks for selecting phrase envelopes and exercising slider gestures in integration tests.

Behavior added:

- Clicking/selecting a phrase envelope exposes an expanded control surface in the Expression Mode side panel.
- The panel shows stored level, attack time, attack start, and attack curve for attack-only envelopes.
- Decay controls appear only when a decay stage exists: decay time, peak, sustain, and decay curve.
- Release controls appear only when a release stage exists: release time, release end, and release curve.
- Slider drags preview by mutating only the current clip expression state during the gesture, then restore the original state and commit one undoable command at drag end.
- Non-drag slider/combo edits still commit normally through the expression command path.

Validation:

```sh
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*phrase envelope*"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_core_tests
```

Results:

- Phrase-envelope focused integration tests: all tests passed, 50 assertions in 2 test cases.
- Expression Mode focused integration tests: all tests passed, 119 assertions in 8 test cases.
- Full core suite: all tests passed, 5566 assertions in 229 test cases.

---

# Prompt 12 - Lane Routing UI

## Goal

Let users route expression lanes to destinations with per-destination min/max mapping.

## Read

- Spec sections 7-9 and 48.1
- Prompt 05 destination registry
- `src/ui/PianoRollComponent.cpp`
- `src/ui/DeviceChainComponent.cpp`
- `src/app/AppServices.*`

## Implement

Left Expression Mode menu:

- routing section for selected lane
- add destination button
- destination picker
- route enable/disable
- remove destination
- min/max mapping controls
- allow inverted mapping by max below min
- mapping preview

Destination picker V1:

- universal/track volume
- pan
- first-party Simple Osc Complex parameters on the selected track
- plugin parameter placeholder if full plugin parameter discovery is too large

## Tests

- add/remove route
- min/max mapping
- inverted mapping
- route to first-party parameter ID
- route UI does not cause playback sync until route is committed

## Non-Goals

- No actual playback application yet unless Prompt 14 is already complete.

## Implementation Status

Implemented Prompt 12 lane routing UI.

Code added:

- Added an `ExpressionRoutingPanel` inside `PianoRollComponent`.
- Added routing owner helpers for destination enumeration, add/remove route, enable/disable, and output-range mutation.
- Added deterministic debug hooks for route add/edit/remove and Simple Osc Complex parameter targeting.
- Extended Expression Mode integration tests for routing behavior.

Behavior added:

- The Expression Mode side panel now includes a routing section for the selected expression lane.
- Destination picker is populated from `expressionDestinationMetadataForTrack`.
- V1 destinations include track volume/pan/pitch-style destinations and first-party Simple Osc Complex expression target parameters when present on the selected track.
- Route rows show destination name/detail, enable toggle, remove button, min/max mapping sliders, and a mapping preview.
- Output min/max can be inverted, so a lane can map high-to-low.
- Route add/remove use existing undoable AppServices route commands.
- Route enable and min/max edits commit through `setClipExpressionState`, preserving undo.

Validation:

```sh
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*routing UI*"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_core_tests
```

Results:

- Routing UI focused integration tests: all tests passed, 34 assertions in 2 test cases.
- Expression Mode focused integration tests: all tests passed, 153 assertions in 10 test cases.
- Full core suite: all tests passed, 5566 assertions in 229 test cases.

---

# Prompt 13 - Generic Cyclic/LFO Expression

## Goal

Add generic cyclic/LFO clips for non-pitch expression lanes.

## Read

- Spec sections 24-29 and 47.3
- Prompt 04 evaluator
- Prompt 08 overlay rendering
- Prompt 12 route UI if present

## Implement

Keyboard behavior:

- `Shift + A + Right/Left`: cyclic attack create/lengthen/shorten
- `Shift + D + Up/Down`: create/increase/decrease max amplitude
- `Shift + R + Left/Right`: release lengthen/shorten
- `Shift + F + Left/Right`: slower/faster rhythmic frequency
- `C + Up/Down`: wave shape
- Delete removes selected cyclic clip

Model behavior:

- only one cyclic expression active per lane/time span
- attack/release/max amplitude
- rhythmic frequency division
- wave shape
- additive/multiplicative blend
- positive oscillator/half-wave polarity
- phase field

UI:

- cyclic clip strip
- expanded cyclic controls
- waveform overlay in composite curve

## Tests

- cyclic create/edit/delete commands
- overlap rejection
- frequency division stepping
- wave shape cycling
- blend and polarity controls
- evaluator output
- paint performance with dense/long cyclic clips

## Non-Goals

- No pitch-lane vibrato yet. That is separate.

## Implementation Status

Implemented the first Prompt 13 generic cyclic/LFO editing slice.

Code added:

- Added shifted Expression Mode keyboard handling for cyclic/LFO clips in `PianoRollComponent`.
- Added active cyclic clip selection state in the piano-roll content.
- Added cyclic edit helpers for create/update/delete using existing expression state and cyclic commands.
- Added debug support for shifted key emulation and cyclic clip counts.
- Extended Expression Mode integration tests with cyclic keyboard coverage.

Behavior added:

- `Shift + A + Right` creates a cyclic expression from the current selected notes and lengthens attack.
- `Shift + A + Left` shortens cyclic attack.
- `Shift + D + Up/Down` increases/decreases cyclic max amplitude.
- `Shift + R + Left/Right` lengthens/shortens cyclic release.
- `Shift + F + Left/Right` selects slower/faster rhythmic frequency divisions.
- `C + Up/Down` cycles the active cyclic clip wave shape when a cyclic clip is selected.
- Delete removes the active cyclic clip before falling back to phrase-envelope delete.
- The existing cyclic strip and composite evaluator now have a keyboard path for user-created clips.

Deferred:

- Expanded cyclic controls are still pending.
- Direct mouse selection of cyclic strips is still pending.
- Blend mode, wave-polarity mode, and phase are model/evaluator-supported but do not yet have UI controls.

Validation:

```sh
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*cyclic keyboard*"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_core_tests
```

Results:

- Cyclic keyboard focused integration test: all tests passed, 31 assertions in 1 test case.
- Expression Mode focused integration tests: all tests passed, 184 assertions in 11 test cases.
- Full core suite: all tests passed, 5566 assertions in 229 test cases.

---

# Prompt 14 - Playback Rendering For Generic Expression Routes

## Goal

Apply prepared generic expression lanes to playback destinations without harming the audio path.

## Read

- Prompt 06 prepared render model
- Prompt 12 route UI/model
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/engine/devices/SimpleOscComplexTracktionPlugin.cpp`
- `src/core/sequencing/AutomationPlayback.cpp`
- `docs/performance-audit/09_TRACKTION_ENGINE_SYNC_AND_PLAYBACK_GRAPH_MATERIALIZATION.md`
- `docs/performance-audit/10_AUTOMATION_EDITING_AND_PLAYBACK_BINDING.md`

## Implement

V1 application order:

1. First-party Simple Osc Complex parameter destinations.
2. Mixer volume/pan if feasible through existing automation application.
3. Plugin parameter destinations only if safe and prepared; otherwise defer with explicit warning/documentation.
4. MIDI CC/pitch bend fallback only after semantic first-party path is stable.

Engine design:

- prepare expression route data during sync
- avoid per-callback model traversal
- avoid parameter lookup by string in audio callback
- pre-resolve first-party device slot and parameter index/ID
- use smoothed parameter/event streams where needed
- do not spam Tracktion edit rebuild for every expression edit if a lighter update path is possible

Simple Osc Complex:

- add realtime-safe parameter modulation input if current patch-only state is insufficient
- separate base patch values from expression modulation values
- smooth zipper-prone destinations

## Tests

Core:

- render mapped route stream
- first-party parameter destination resolution

Engine/integration:

- expression lane modulates Simple Osc Complex `amp.level`
- expression lane modulates `osc.pm.amount`
- route disabled does nothing
- bypassed lane does nothing
- playback render remains finite/bounded

Performance:

- playback sync with many expression routes
- audio render with expression events and dense notes

## Non-Goals

- No slurs.
- No pitch vibrato.
- No third-party VST parameter automation unless it is proven safe.

## Implementation Status

Implemented the first Prompt 14 playback-rendering slice for first-party Simple Osc Complex destinations.

Code added:

- Added prepared expression modulation streams to `SimpleOscComplexTracktionPlugin`.
- Kept Simple Osc Complex base patch state separate from expression modulation values.
- Added sync-time expression preparation in `TracktionPlaybackEngine`.
- Resolved first-party device routes to native plugin instances once during sync.
- Converted prepared tick-domain route segments into edit-time seconds before playback.
- Cleared and replaced native modulation streams at sync boundaries.
- Made disabled expression lanes produce no prepared route output stream.

Behavior added:

- Expression routes can modulate Simple Osc Complex parameters such as `amp.level`, `osc.pm.amount`, and `wavefolder.amount` during playback.
- Route data is prepared outside the audio callback; the callback samples already-installed native streams and does not traverse the project model.
- Disabled lanes and unavailable routes do not generate playback modulation streams.
- Third-party plugin parameter, MIDI/pitch, and mixer expression routes are not mapped in this slice; the engine emits explicit sync warnings when those route types have prepared playback data.

Deferred:

- Third-party VST parameter modulation remains deferred until a safe prepared automation path is proven.
- MIDI CC, pitch bend, pitch slur, and vibrato playback remain deferred to the later pitch/MIDI prompts.
- Mixer volume/pan expression playback remains deferred; existing timeline automation still handles mixer automation.
- First-party-device in-place sync is still a performance follow-up because the current engine rebuild path still treats first-party devices as full-sync material.
- Sample-accurate smoothing is not yet implemented; the native plugin applies prepared segment values at render subsegment starts.

Validation:

```sh
cmake --build build --target tsq_core_tests -j 6
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_core_tests "*Prepared expression*"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_core_tests
./build/tests/tsq_engine_integration_tests "[baseline][perf]"
```

Results:

- Prepared expression focused core tests: all tests passed, 52 assertions in 5 test cases.
- Expression Mode focused integration tests: all tests passed, 184 assertions in 11 test cases.
- Full core suite: all tests passed, 5574 assertions in 230 test cases.
- Expression baseline performance probes: all tests passed, 24 assertions in 2 test cases.

---

# Prompt 15 - Pitch Lane Core Model And Trajectory Evaluator

## Goal

Implement pitch-lane data structures and pure core trajectory evaluation for slurs and vibrato.

## Read

- Spec sections 30-46
- Prompt 01 pitch placeholders
- `src/core/sequencing/MidiNote.h`
- `src/core/sequencing/MidiClip.h`
- `src/core/devices/SimpleOscComplexSynth.*`

## Implement

Core:

- `PitchSlur`
- slur blocks
- per-voice override state
- `VibratoExpression`
- vibrato blocks
- per-voice overrides
- pairing by register for chord slurs
- voice trajectory helper:
  - base note pitch
  - slur transition
  - vibrato layer
  - final pitch in semitones/cents over time

Rules:

- slurs reference source/destination note IDs
- basic `S` creates legato-only slur time 0
- chord slurs pair by register
- block settings are shared until a voice override is edited
- vibrato phase continues across note changes

## Tests

- note pairing by register
- two-note slur creation
- chord slur block creation
- shared block edit
- per-voice override copy/diverge
- slur trajectory curves
- vibrato phase continuity
- vibrato on top of slur
- polyphonic trajectory independence

## Non-Goals

- No synth playback yet.
- No UI overlay yet beyond possible debug helpers.

## Implementation Status

Implemented the Prompt 15 pure core pitch-lane trajectory slice.

Code added:

- Added `PitchExpressionEvaluation` core helpers.
- Added register-based note pairing for chord slurs.
- Added legato zero-time slur block creation using shared `ExpressionBlockId`.
- Added shared slur block edit helpers that skip diverged per-voice overrides.
- Added per-voice slur override helper that copies block-like settings into one voice and marks it as overridden.
- Added pure pitch voice trajectory evaluation:
  - base MIDI note pitch
  - slur transition offset
  - vibrato offset
  - final semitone pitch
- Added mutable `ExpressionLane::findPitchSlur` and `ExpressionLane::findVibratoExpression` lookups for command/UI follow-up prompts.

Behavior added:

- Chord slurs pair lowest source to lowest destination, middle to middle, highest to highest.
- Basic slur block creation produces legato, no-retrigger, zero-time pitch slurs.
- Shared slur edits apply to all block voices until a specific voice is overridden.
- Slur trajectories begin at the destination note start and interpolate from source pitch to destination pitch over `slurTime`.
- Vibrato phase is evaluated from the phrase region start, so it remains continuous across note changes within one vibrato expression.
- Vibrato layers on top of slur pitch motion.
- Polyphonic slur voices can diverge independently once overridden.

Deferred:

- Persistent first-class slur/vibrato block containers remain deferred; V1 uses existing object `blockId` references plus helper-applied shared settings.
- Synth playback/event contracts remain deferred to Prompt 16.
- UI creation/editing/overlay remains deferred to Prompts 17 and 18.
- Pitch-bend/MIDI fallback export is not implemented here.

Validation:

```sh
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "*Pitch expression*"
./build/tests/tsq_core_tests
cmake --build build --target tsq_engine_integration_tests -j 6
```

Results:

- Pitch expression focused core tests: all tests passed, 33 assertions in 7 test cases.
- Full core suite: all tests passed, 5607 assertions in 237 test cases.
- Engine integration target built successfully. The build emitted one existing unused-variable warning in `VstStateRegressionTest.cpp`.

---

# Prompt 16 - Native Synth Expression Event Contract

## Goal

Define and implement the first-party synth event API needed for pitch lane expression.

## Read

- `src/core/devices/SimpleOscComplexSynth.h`
- `src/core/devices/SimpleOscComplexSynth.cpp`
- `src/engine/devices/SimpleOscComplexTracktionPlugin.*`
- `docs/FIRST_PARTY_DEVICES_AND_SIMPLE_OSC_COMPLEX.md`
- Prompt 15 trajectory evaluator

## Implement

Add note-ID-aware event support:

- note on with note ID
- note off with note ID
- legato/no-retrigger instruction
- slur instruction source/destination note IDs
- per-voice pitch offset stream/segment
- vibrato instruction or precomputed pitch trajectory stream

Voice allocator:

- preserve note ID to voice relationship
- support release voices
- handle voice stealing deterministically
- handle slur from an active/releasing source note to destination note

Audio thread rules:

- event queues must be preallocated or owned by Tracktion/MIDI buffers in a safe way
- no dynamic allocation in render
- no map/string lookup in render
- no locks in render

## Tests

DSP/core tests:

- note ID voice tracking
- legato no-retrigger keeps envelope continuity
- slur pitch reaches destination
- voice stealing deterministic
- release voice behavior
- no NaN/inf under rapid slurs/polyphony

## Non-Goals

- No UI.
- No third-party plugin pitch fallback.

## Implementation Status

Implemented the Prompt 16 native synth expression event contract slice.

Code added:

- Added numeric `SimpleOscComplexNoteId` handles for realtime note-ID-aware voice tracking.
- Added note-ID overloads for `SimpleOscComplexSynth::noteOn` and `noteOff`.
- Added `startLegatoSlur` for source-note-ID to destination-note-ID pitch motion.
- Added per-voice pitch offset input via `setVoicePitchOffset`.
- Added debug/query helpers for active note-ID voice counts, current voice pitch, and envelope level.
- Added deterministic voice stealing that prefers release voices, then older active voices.
- Added thin `SimpleOscComplexTracktionPlugin` pass-through methods for expression note-on, note-off, legato slur, and per-voice pitch offset.

Behavior added:

- Note IDs remain attached to voices through note-on, release, and legato slur transitions.
- Legato/no-retrigger slurs reuse the source voice and preserve envelope continuity.
- Slurs interpolate from the source voice's current pitch to the destination MIDI pitch using the requested expression curve.
- Destination note ID replaces the source note ID after a legato slur, so later note-off and pitch-offset events can address the continued voice.
- Slurs can target active or releasing source voices.
- Existing anonymous MIDI note-on/note-off behavior remains available for non-expression playback.
- Realtime path uses fixed voice storage and numeric handles; no string map lookup or dynamic event allocation was introduced inside `render`.

Deferred:

- Prepared project note-ID string to numeric handle resolution is deferred to Prompt 17 playback integration.
- Scheduling expression events from Tracktion MIDI/project data is deferred to Prompt 17.
- Precomputed vibrato/pitch trajectory streams are not installed into playback yet.
- Third-party plugin pitch fallback remains out of scope.

Validation:

```sh
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "[devices][simple-osc-complex][expression]"
./build/tests/tsq_core_tests
cmake --build build --target tsq_engine_integration_tests -j 6
```

Results:

- Simple Osc Complex expression-focused core tests: all tests passed, 279 assertions in 6 test cases.
- Full core suite: all tests passed, 5886 assertions in 243 test cases.
- Engine integration target built successfully.

---

# Prompt 17 - Slur UI And Playback Integration

## Goal

Add user-facing slur creation/editing and connect it to first-party synth playback.

## Read

- Spec sections 31-37 and 47.4
- `src/ui/PianoRollComponent.cpp`
- Prompt 15 core model
- Prompt 16 synth event contract
- `src/engine/TracktionPlaybackEngine.cpp`

## Implement

UI:

- `S` with two selected notes creates legato-only slur.
- `S` with selected chord groups creates register-paired slur block.
- `S + Right/Left` changes slur time.
- `C + Up/Down` changes slur curve.
- Delete removes selected slur.
- draw slur center-to-center for readability.
- expanded slur menu shows shared block controls and per-voice overrides.

Playback:

- prepare slur instructions during sync.
- send semantic slur/no-retrigger events to Simple Osc Complex.
- no visible MIDI timing mutation.

## Tests

- UI command creates slur object
- slur block pairing
- edit shared block
- edit individual voice override
- playback uses legato/no-retrigger with Simple Osc Complex
- audio remains finite/bounded

## Non-Goals

- No vibrato.
- No third-party portamento fallback.

## Implementation Status

Implemented the Prompt 17 slur creation/editing and first-party playback integration slice.

Code added:

- Added piano roll pitch-slur selection state and debug hooks.
- Added `S` creation for two selected note groups:
  - one source/destination pair creates a legato/no-retrigger pitch slur.
  - selected chord groups create a shared register-paired slur block.
- Added `S + Left/Right` slur-time edits.
- Added `C + Up/Down` slur-curve edits for the selected slur.
- Added Delete removal for selected slurs.
- Added center-to-center slur drawing over visible note bounds.
- Added prepared native pitch-event streams to `SimpleOscComplexTracktionPlugin`.
- Added Tracktion sync preparation for Simple Osc Complex note-on, note-off, and semantic slur events using stable numeric note IDs.
- Suppressed normal MIDI note materialization for first-party Simple Osc Complex tracks so slur destinations are not retriggered by duplicate MIDI note-ons.
- Refreshed prepared first-party pitch/modulation streams in both full sync and in-place sync paths.

Behavior added:

- Slur creation requires exactly two selected rhythmic groups and pairs chord voices by register.
- Slur block edits apply shared settings when the selected slur has no voice override.
- Voice override edits affect only the selected overridden slur voice.
- Simple Osc Complex receives destination slurs as native legato/no-retrigger events, while destination note-offs remain scheduled against the destination note ID.
- No MIDI note timing is mutated to represent slurs.

Deferred:

- The expanded visual slur menu/panel for shared block controls and per-voice override controls is still deferred. The underlying shared/override model and keyboard editing are in place.
- Third-party portamento fallback remains out of scope.
- Vibrato remains out of scope for Prompt 18.

Validation:

```sh
cmake --build build --target tsq_core_tests tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*[slur]*"
./build/tests/tsq_core_tests "[devices][simple-osc-complex][expression]"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
```

Results:

- Slur UI integration tests: all tests passed, 47 assertions in 2 test cases.
- Simple Osc Complex expression-focused core tests: all tests passed, 279 assertions in 6 test cases.
- Expression Mode integration tests: all tests passed, 231 assertions in 13 test cases.
- Build succeeded. Existing JUCE assertion logging appears during headless integration startup, matching previous test runs.

---

# Prompt 18 - Pitch-Layer Vibrato UI And Playback

## Goal

Implement phrase-wide vibrato in the Pitch expression lane.

## Read

- Spec sections 38-46 and 47.5
- Prompt 15 trajectory evaluator
- Prompt 16 synth event contract
- Prompt 17 slur UI

## Implement

Keyboard behavior:

- `Shift + D + Up/Down`: create/increase/decrease vibrato amplitude
- `Shift + A + Right/Left`: attack time
- `Shift + R + Left/Right`: release time
- `Shift + F + Left/Right`: frequency division
- `C + Up/Down`: wave shape
- Delete removes selected vibrato

UI:

- draw vibrato over note/voice path
- draw vibrato on top of slur trajectory
- hybrid visual scaling:
  - exaggerate subtle vibrato
  - keep wide gestures closer to pitch scale
- show true depth numerically in cents/semitones
- shared block plus per-voice overrides

Playback:

- vibrato phase continues across notes
- shared phase by default for polyphonic selection
- per-voice overrides can diverge
- send semantic/prepared pitch data to Simple Osc Complex

## Tests

- vibrato create/edit/delete
- frequency stepping
- wave shape cycling
- shared phase across voices
- per-voice override
- vibrato rides on slur
- playback render finite/bounded
- UI paint performance with many vibrato paths

## Non-Goals

- No generic cyclic lane changes.
- No third-party pitch bend fallback unless already designed.

## Implementation Status

Implemented the Prompt 18 pitch-lane vibrato slice.

Code added:

- Added piano roll pitch-lane vibrato selection/debug hooks.
- Added Pitch-lane-specific keyboard behavior:
  - `Shift + D + Up/Down` creates/increases/decreases vibrato depth.
  - `Shift + A + Right/Left` changes vibrato attack time.
  - `Shift + R + Left/Right` changes vibrato release time.
  - `Shift + F + Left/Right` steps vibrato frequency division.
  - `C + Up/Down` cycles vibrato wave shape when a vibrato is selected.
  - Delete removes the selected vibrato.
- Preserved existing cyclic-expression Shift-key behavior on non-pitch lanes.
- Added lightweight vibrato drawing over source notes with hybrid-scaled depth and a cents label for selected vibrato.
- Added prepared pitch-offset events to `SimpleOscComplexTracktionPlugin`.
- Added Tracktion sync sampling from `evaluatePitchVoiceTrajectoryAt` so vibrato sends prepared per-note pitch offsets to Simple Osc Complex.
- Routed sampled vibrato offsets across slur destination note IDs so vibrato can ride on legato slur voices after the slur transfers voice ownership.

Behavior added:

- Pitch-lane vibrato is phrase-wide over the selected note region.
- Polyphonic selections share one vibrato expression and default phase.
- Multi-note vibrato expressions receive a shared block ID.
- Per-voice override metadata is supported and covered through debug integration hooks.
- First-party playback uses numeric note IDs and prepared offset events; no third-party pitch-bend fallback was added.

Deferred:

- A full expanded vibrato control panel/menu remains deferred. The model, keyboard editing, drawing, shared phase, and per-voice override data are in place.
- The vibrato visual is intentionally lightweight; richer slur-trajectory-aware drawing can be refined later.
- Generic cyclic lanes and third-party pitch fallback remain out of scope.

Validation:

```sh
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*[vibrato]*"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_core_tests "[devices][simple-osc-complex][expression]"
```

Results:

- Vibrato UI integration tests: all tests passed, 44 assertions in 2 test cases.
- Expression Mode integration tests: all tests passed, 275 assertions in 15 test cases.
- Simple Osc Complex expression-focused core tests: all tests passed, 279 assertions in 6 test cases.
- Build succeeded. Existing JUCE assertion logging appears during headless integration startup, matching previous test runs.

---

# Prompt 19 - Expression Route Playback Beyond First-Party Devices

## Goal

Expand generic expression routing beyond first-party synth parameters where safe.

## Read

- Prompt 14 implementation
- `src/engine/TracktionPlaybackEngine.cpp`
- `src/core/sequencing/AutomationPlayback.cpp`
- `src/ui/TimelineComponent.cpp`
- plugin parameter debug/protection code in `TracktionPlaybackEngine.cpp`

## Implement

Destinations in priority order:

1. Track/mixer volume.
2. Pan.
3. Send level.
4. MIDI CC to instrument track.
5. Plugin parameter automation only if parameter identity and state safety are robust.
6. Pitch bend fallback for third-party synths only after first-party pitch path is stable.

Rules:

- Do not reintroduce VST parameter wipe risks.
- Do not write plugin state blobs in audio callback.
- Pre-resolve plugin parameter indices.
- Protect user-edited plugin state.
- Prefer existing automation application pathways where they are safe.

## Tests

- expression to track volume
- expression to pan
- expression to send level if routing exists
- expression to MIDI CC event stream
- plugin parameter route skipped or works with explicit test plugin
- no plugin-state reset regression

## Non-Goals

- No new UI unless route picker needs new destination rows.

## Implementation Status

Implemented the safe mixer-playback slice of Prompt 19.

Code added:

- Added a prepared expression playback model to `TracktionPlaybackEngine` sync finalization.
- Reused the existing message-thread automation playback timer to apply expression route values at the playhead.
- Added prepared expression route evaluation for mixer destinations:
  - track volume
  - track pan
  - send level
- Reused existing safe mixer automation application pathways.
- Kept expression mixer playback out of the audio callback.
- Added narrow `TracktionPlaybackEngine` debug accessors for track volume, pan, and send gain so integration tests can verify actual Tracktion plugin state.
- Fixed `AuxSendPlugin` application order so unmuting does not restore over the intended expression send gain.

Behavior added:

- Expression routes to volume/pan/send now play back when the playhead moves and while transport playback runs.
- Ordinary automation still applies first, then expression mixer routes apply their current prepared value.
- First-party device parameter expression streams remain handled by the native Simple Osc path from earlier prompts.
- Plugin parameter routes remain skipped to avoid reintroducing VST parameter wipe risks.

Deferred:

- MIDI CC route materialization is deferred. Tracktion has controller-event APIs, but this needs a focused clip/repetition materialization pass and event-output tests.
- Third-party plugin parameter expression playback is deferred until parameter identity/state protection can be tested with an explicit safe test plugin.
- Pitch bend fallback for third-party synths remains deferred until it has a robust event model and tests.

Validation:

```sh
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*[expression][playback][mixer]*"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_engine_integration_tests "*VST state*"
```

Results:

- Expression mixer playback tests: all tests passed, 17 assertions in 2 test cases.
- Expression Mode integration tests: all tests passed, 275 assertions in 15 test cases.
- VST state regression filter: all tests passed, 16 assertions in 1 test case.
- Build succeeded. Existing JUCE assertion logging appears during headless integration startup, matching previous test runs.

---

# Prompt 20 - Expression Clipboard, Duplication, And Lane Presets

## Goal

Plan and implement limited reuse workflows without violating the spec’s non-destructive phrase binding.

## Read

- Spec sections 3, 12, and 49
- Current note clipboard behavior in `PianoRollComponent.cpp`
- Prompt 01 model

## Implement

V1 should not freely copy/paste phrase expression clips independently from source notes.

Allowed:

- duplicate lane routing/mapping
- save lane preset without note-bound phrase objects
- copy expression settings from selected envelope to another selected phrase by explicit command, if desired
- copy pitch block settings between selected slur/vibrato objects

Forbidden for V1:

- paste detached phrase envelope clips to arbitrary time without source notes
- treat expression clips like arrangement clips

## Tests

- lane preset excludes note-bound phrase objects
- settings copy preserves constraints
- note copy/paste does not accidentally duplicate expression unless explicitly designed

## Non-Goals

- No broad preset browser unless requested later.

## Implementation Status

Implemented the core reuse-safety slice of Prompt 20.

Code added:

- Added `ExpressionLanePreset` as a lane settings/routing snapshot.
- Added helpers to create a preset from an expression lane, create a lane from a preset, and duplicate lane routing/mapping.
- Lane presets intentionally copy lane name, polarity, enabled state, and routes only.
- Lane presets intentionally exclude phrase envelopes, cyclic clips, pitch slurs, and vibrato objects.
- Added explicit settings-copy helpers for phrase envelopes, pitch slurs, and vibrato expressions.
- Settings-copy helpers preserve target IDs, source note IDs, destination note IDs, and phrase regions.
- Phrase envelope and vibrato settings copies fail without mutating the target when copied timing would not fit the target phrase duration.
- Vibrato settings copy does not copy per-voice overrides, because those are note-bound metadata.

Behavior added:

- Reuse workflows can duplicate routing/mapping without creating detached expression clips.
- Users can later get explicit "copy settings to selected expression" commands without violating the spec's phrase-binding rule.
- The core API shape now makes the V1 forbidden behavior harder to accidentally implement.

Deferred:

- No browser UI or saved preset library was added.
- No piano-roll clipboard integration was added for expression objects.
- No command-stack wrappers were added yet for the settings-copy helpers.

Validation:

```sh
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "*[expression][phrase]*"
./build/tests/tsq_core_tests "*[expression][pitch][copy]*"
./build/tests/tsq_core_tests "*Expression*"
./build/tests/tsq_core_tests "*[expression]*"
```

Results:

- Phrase expression tests: all tests passed, 75 assertions in 5 test cases.
- Pitch settings copy tests: all tests passed, 23 assertions in 1 test case.
- Expression-named core tests: all tests passed, 627 assertions in 41 test cases.
- Expression-tagged core tests: all tests passed, 378 assertions in 13 test cases.

---

# Prompt 21 - Export And Offline Render Semantics

## Goal

Ensure expression data survives export/render workflows with the correct target behavior.

## Read

- `src/core/midi/MidiExporter.cpp`
- `src/core/midi/MidiFileWriter.*`
- `src/engine/TracktionPlaybackEngine.cpp`
- Expression playback prompts

## Implement

Export strategy:

- First-party audio render should consume semantic expression events.
- MIDI export can optionally render generic lanes to MIDI CC/pitch bend where routes target MIDI destinations.
- Do not bake first-party-only slur/vibrato semantics into plain MIDI unless a deliberate fallback is designed.
- Document unsupported export semantics clearly.

## Tests

- project with expression lanes exports without crashing
- MIDI CC route exports CC events if supported
- unsupported semantic pitch expression is preserved in project but omitted/warned in plain MIDI export

## Non-Goals

- No full offline audio bounce engine unless already present.

## Implementation Status

Implemented the Prompt 21 plain-MIDI export semantics slice.

Code added:

- Added opt-in `MidiExportOptions::renderExpressionMidiCcRoutes`.
- Added `MidiExportOptions::expressionRenderStep` for prepared expression route sampling during MIDI export.
- Added `MidiExportReport` so export callers can inspect non-fatal warnings.
- Added project-aware `MidiExporter::exportClipToBytes` and `exportClipToFile` overloads that can resolve expression route availability.
- Reused `prepareExpressionClipRenderData` to render generic expression routes to MIDI export events.
- Rendered available `ExpressionDestination::midiCc` routes as MIDI Control Change events when expression MIDI CC export is explicitly enabled.
- Preserved the existing clip-only MIDI export API as note/meta-event export.

Behavior added:

- Projects with expression lanes export to MIDI without crashing.
- MIDI CC expression routes can be baked into plain MIDI as CC events when explicitly requested.
- Semantic pitch slurs and vibrato remain stored in the project but are not baked into plain MIDI.
- First-party, plugin-parameter, mixer, pitch, and pitch-bend routes emit export warnings rather than silently pretending they were represented in plain MIDI.
- Existing UI/file callers that use the clip-only overload keep the previous note-only behavior.

Deferred:

- No offline first-party audio render/bounce engine was added.
- No MIDI pitch-bend fallback for first-party slur/vibrato was added.
- No plugin-parameter or mixer automation MIDI encoding was added.
- Main UI export still uses the legacy clip-only path; wiring the project-aware report into UI messaging is a later UI slice.

Validation:

```sh
cmake --build build --target tsq_core_tests -j 6
./build/tests/tsq_core_tests "MIDI exporter*"
./build/tests/tsq_core_tests "*[expression]*"
./build/tests/tsq_core_tests "*Expression*"
./build/tests/tsq_core_tests "*Serialization*"
```

Results:

- MIDI exporter tests: all tests passed, 63 assertions in 8 test cases.
- Expression-tagged core tests: all tests passed, 378 assertions in 13 test cases.
- Expression-named core tests: all tests passed, 653 assertions in 43 test cases.
- Serialization filter: all tests passed, 13 assertions in 2 test cases.

---

# Prompt 22 - Full Feature Performance Pass

## Goal

Audit Expression Mode end-to-end for responsiveness and audio safety.

## Read

- All expression implementation docs
- `docs/performance-audit/`
- Prompt 00 baseline
- all new expression tests/probes

## Measure

Scenarios:

- empty clip
- 100 notes, 2 lanes
- 1,000 notes, 8 lanes
- dense phrase envelopes
- dense cyclic LFOs
- many slur blocks
- polyphonic vibrato
- first-party parameter modulation
- playback sync after expression edit
- playback start from beat one
- return to zero then play
- device parameter edits plus expression edits

Hot paths:

- piano-roll paint
- expression overlay sampling/path generation
- marquee selection with release ghosts
- keyboard editing
- command execute/undo
- serialization save/load
- playback sync/preparation
- Simple Osc Complex render with expression events

## Implement

- Cache missing hot paths.
- Add dirty category precision where needed.
- Avoid whole-project expression rebuilds when one clip/lane changes.
- Add prepared data reuse fingerprints.
- Add diagnostics trace labels for expression phases.
- Fix any audio-thread violations found.

## Tests

- focused performance probes
- full core tests
- relevant integration tests
- debug app build and smoke launch
- manual QA checklist update

## Done When

- Expression Mode feels responsive on dense projects.
- Audio remains stable and clean.
- No realtime path violates the guardrails.
- Remaining risks are documented.

## Implementation Status

Implemented the Prompt 22 measured performance-pass slice.

Code added:

- Added prepared-expression performance trace labels in `PreparedExpressionRenderModel`.
- Added Tracktion expression preparation and native route installation trace labels.
- Removed duplicate prepared-expression render model construction during Tracktion sync.
- Reused the same prepared expression model for first-party native modulation installation and mixer playback route state.
- Added a dense Expression Mode integration performance probe covering:
  - 100 notes / 2 lanes
  - 1,000 notes / 8 lanes
  - phrase envelopes
  - cyclic LFOs
  - pitch slurs
  - first-party modulation routes
  - mixer routes
  - return-to-zero
  - playback start
  - sync after note edit

Trace run:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./build/tests/tsq_engine_integration_tests "*[expression][perf][sync]*"
```

Representative debug results:

- 100 notes / 2 lanes prepared expression render model: 3.456 ms.
- 1,000 notes / 8 lanes prepared expression render model: 19.984 ms.
- 1,000 notes / 8 lanes native route stream installation: 0.186 ms.
- 1,000 notes / 8 lanes full sync: 30.237 ms.
- 1,000 notes / 8 lanes return-to-zero: 0.234 ms.
- 1,000 notes / 8 lanes playback start: 208.286 ms.
- 1,000 notes / 8 lanes sync after note edit: 33.610 ms.

Documentation:

- Added `docs/performance-audit/20_EXPRESSION_MODE_FULL_PERFORMANCE_PASS.md`.

Validation:

```sh
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*[expression][perf][sync]*"
./build/tests/tsq_engine_integration_tests "*[expression][playback][mixer]*"
./build/tests/tsq_engine_integration_tests "*[expression][baseline][perf]*"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_core_tests "*[expression]*"
./build/tests/tsq_core_tests "*Expression*"
cmake --build build --target tsq_app -j 6
```

Results:

- Dense expression sync/playback-start probe: all tests passed, 31 assertions in 1 test case.
- Expression mixer playback tests: all tests passed, 17 assertions in 2 test cases.
- Expression baseline performance probes: all tests passed, 24 assertions in 2 test cases.
- Expression Mode integration filter: all tests passed, 306 assertions in 16 test cases.
- Expression-tagged core tests: all tests passed, 378 assertions in 13 test cases.
- Expression-named core tests: all tests passed, 653 assertions in 43 test cases.
- Debug app target built successfully.

Remaining risks:

- Playback start remains the largest measured expression-adjacent spike, but the trace suggests it is broader than expression preparation.
- Prepared expression data is still rebuilt at project sync scope. A future cache should reuse per-clip/lane prepared data by fingerprint.
- Expression-only command dirty categories should get a dedicated AppServices/UI pass so display-only expression edits do not force clip materialization.
- The app target has no safe one-shot CLI smoke mode; integration probes covered app services, UI components, and Tracktion startup.

---

# Prompt 23 - Manual QA And Product Polish

## Goal

Make the feature feel musical, discoverable, and coherent after the core engineering is complete.

## Read

- `docs/MANUAL_QA_CHECKLIST.md`
- `docs/FEATURES.md`
- all expression docs

## Implement

- Update manual QA checklist.
- Update feature list.
- Polish labels/tooltips.
- Confirm keyboard behavior does not conflict with ordinary piano roll.
- Confirm notes fade but remain readable.
- Confirm stage colors are accessible.
- Confirm release ghost notes are clearly secondary.
- Confirm route mapping UI is understandable.
- Confirm Simple Osc Complex expression targets feel stable and musical.

## Tests

- smoke launch debug app
- manual workflows:
  - create lane
  - route to Simple Osc Complex
  - create phrase envelope
  - create cyclic expression
  - create slur
  - create vibrato
  - save/load project
  - undo/redo edits
  - return to zero and play

## Done When

- The feature can be demoed without explaining implementation details.
- The project file survives save/load.
- Playback sounds correct.
- Editing remains fast.

## Implementation Status

Implemented the Prompt 23 documentation and UI polish slice.

Code/UI polish:

- Clarified the Expression Mode toggle tooltip.
- Renamed the expression release toggle from `Release Mode` to `Release Tails`.
- Renamed the expression routing panel title from `Routing` to `Routes`.
- Clarified routing tooltips for destination selection, add/remove route, route enable, and route output ranges.
- Added tooltips to phrase envelope labels, sliders, and curve selectors.
- Added missing tooltips to chromatic reveal, chromatic transpose, and scale-degree transpose controls.
- Kept the UI text compact so the piano-roll toolbar and expression lane panel remain work-focused.

Documentation:

- Added a full `Expression Mode` section to `docs/MANUAL_QA_CHECKLIST.md`.
- Added shipped Expression Mode features to `docs/FEATURES.md`.
- Added explicit Expression Mode limitations for deferred third-party/plugin/pitch-bend/plain-MIDI semantics.

Validation:

```sh
cmake --build build --target tsq_engine_integration_tests -j 6
./build/tests/tsq_engine_integration_tests "*[expression][ui]*"
./build/tests/tsq_engine_integration_tests "*Expression Mode*"
./build/tests/tsq_core_tests "*[expression]*"
```

Results:

- Expression UI integration tests: all tests passed, 251 assertions in 13 test cases.
- Expression Mode integration filter: all tests passed, 306 assertions in 16 test cases.
- Expression-tagged core tests: all tests passed, 378 assertions in 13 test cases.
- Existing headless JUCE assertion logging appears during integration startup/paint, matching previous runs.

---

## Suggested First Milestone Cut

The first shippable internal milestone should stop after Prompt 14:

- stored expression lanes
- default Volume/Pitch lanes
- phrase envelopes
- cyclic generic expression
- route mapping to Simple Osc Complex parameters
- overlay rendering
- prepared generic playback modulation

Do not include slurs/vibrato in the first milestone unless the earlier prompts are genuinely complete and performance is already proven.

## Suggested Second Milestone Cut

The second milestone should cover pitch semantics:

- pitch lane model
- note-ID-aware synth event contract
- slurs
- polyphonic slur blocks
- vibrato
- per-voice overrides

This milestone is where the first-party synth architecture pays off. It should not be rushed into a generic automation system.

## Open Questions To Resolve During Implementation

- Should expression state live only on `MidiClip`, or should there also be reusable track/project lane presets in V1?
- How precise should the prepared control stream be for generic parameter modulation?
- Should generic routes update through Tracktion automation lanes, direct plugin parameter writes, or native device modulation inputs?
- What is the safest V1 behavior for third-party plugin parameter destinations?
- How should expression edits preview during playback without excessive sync churn?
- What is the visual style for lane list, route editor, and expanded menus within the current piano-roll toolbar constraints?
- What is the exact release ghost opacity/color in the existing palette?
- Should pitch-lane vibrato amplitude be stored in cents, semitones, or a richer pitch-distance type?
- How much third-party MIDI export fallback is desirable in V1?

## Final Reminder

Expression Mode should not become “automation with a new coat of paint.”

It is a musical performance layer. The stored data should remain meaningful to a musician, and the playback layer should convert that intent into clean, efficient control data only at the right boundary.

## Closeout

The implementation closeout and resolved/deferred architecture notes are tracked in:

- `docs/EXPRESSION_MODE_CLOSEOUT.md`
