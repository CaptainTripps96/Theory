# TheorySequencer Implementation Log

## Prompt 01 - Create CMake Project Skeleton

Summary:
- Created the root CMake project and module-level CMake files for `tsq_core`, `tsq_engine`, `tsq_ui`, and `tsq_app`.
- Added dependency validation for local `deps/JUCE`, `deps/tracktion_engine`, and `deps/Catch2` checkouts, with clear configure-time errors when missing.
- Added a Catch2-based `tsq_core_tests` target with a tiny placeholder test.
- Added README build prerequisites and configure/build/test commands.
- Installed pinned local dependency checkouts:
  - JUCE `8.0.13` at `deps/JUCE`
  - Tracktion Engine `v3.2.0` at `deps/tracktion_engine`
  - Catch2 `v3.15.0` at `deps/Catch2`
- Adjusted the placeholder Catch2 assertion to compare a `std::string` instead of relying on Catch2 `std::string_view` stringification.

Commands run:
- `git clone --depth 1 --branch 8.0.13 https://github.com/juce-framework/JUCE.git deps/JUCE`
- `git clone --depth 1 --branch v3.2.0 https://github.com/Tracktion/tracktion_engine.git deps/tracktion_engine`
- `git clone --depth 1 --branch v3.15.0 https://github.com/catchorg/Catch2.git deps/Catch2`
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

Result:
- Configure succeeded.
- Build succeeded.
- `tsq_core_tests` passed: 1 test, 0 failures.

Known issues:
- Persistent docs requested by Prompt 01 (`docs/AI_CONTEXT.md`, `docs/ARCHITECTURE.md`, `docs/CODING_STANDARDS.md`, and `docs/AUDIO_THREAD_RULES.md`) are not present yet; this pass used `docs/agent/*` as the source of truth.

## Prompt 02 - Add JUCE Application Shell

Summary:
- Added the first runnable JUCE desktop application shell.
- Created `TheorySequencerApplication`, `MainWindow`, and `MainComponent`.
- Converted `tsq_app` into a JUCE GUI app target while preserving the target name.
- Added a native desktop window with a dark placeholder UI showing:
  - `TheorySequencer`
  - `VST3 + Scale-Aware MIDI Sequencer`
- Kept this prompt scoped to the app shell; no Tracktion Engine initialization, plugin scanning, piano roll, app services, diagnostics, or product editing features were added.
- Enabled C in the root CMake project because JUCE's generated GUI target compiles C helper sources.
- Fixed a JUCE 8 compile detail by using ordinary `const` colour objects instead of `constexpr juce::Colour`.

Commands run:
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer`

Result:
- Configure succeeded.
- Build succeeded.
- `tsq_core_tests` passed: 1 test, 0 failures.
- Launch smoke test succeeded: the app process started, reported `JUCE v8.0.13`, stayed running for the smoke-test interval, and was stopped cleanly.

Known issues:
- The window display was implemented and the app launch was smoke-tested, but no manual visual screenshot verification was captured in this pass.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 03 - Add Logging, Diagnostics, and App Services Skeleton

Summary:
- Added `src/core/diagnostics/Logger.h/.cpp`, a small JUCE-free logger that stores log entries and writes formatted messages to console output.
- Added `src/app/AppServices.h/.cpp` as the explicit app-owned service container placeholder for future audio engine, project service, and plugin scanner ownership.
- Wired `AppServices` into `TheorySequencerApplication`, `MainWindow`, and `MainComponent` through constructor injection; no global mutable service singleton was added.
- Added a diagnostics area to the placeholder UI showing startup diagnostics from `AppServices`.
- Added core unit tests for diagnostic log-level naming and Logger storage/formatting.
- Kept this prompt scoped to diagnostics/services; no Tracktion Engine initialization, plugin scanning, audio device handling, or project mutation code was added.

Commands run:
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer`

Result:
- Configure succeeded.
- Build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Launch smoke test succeeded. Console diagnostics showed:
  - `[info] App started`
  - `[info] Build type: Debug`
  - `[info] Platform: macOS`

Known issues:
- The diagnostics panel was implemented and startup diagnostics were verified via console output, but no screenshot-based visual verification was captured in this pass.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 04 - Integrate Tracktion Engine Behind PlaybackEngine Interface

Summary:
- Added `src/engine/PlaybackEngine.h` and `src/engine/EngineTypes.h` as TheorySequencer-owned, JUCE/Tracktion-free playback abstractions.
- Added `src/engine/TracktionPlaybackEngine.h/.cpp` as the Tracktion-backed implementation.
- Added Tracktion modules to CMake via `deps/tracktion_engine/modules`; Tracktion-specific headers remain isolated to `/engine`.
- Updated `AppServices` to explicitly own and initialize a `TracktionPlaybackEngine`.
- Added an engine status area and Start/Stop buttons to the placeholder UI, wired through `PlaybackEngine` rather than Tracktion internals.
- Created a minimal Tracktion `Engine` and single-track `Edit` for transport control; no project model sync, plugin scanning, plugin loading, audio recording, or product editing features were added.
- Overrode Tracktion's default behavior to avoid opening audio input by default for this prompt.
- Moved Tracktion Engine from release tag `v3.2.0` to exact commit `2877b62` because the tag did not compile against JUCE `8.0.13`'s updated `AudioFormat` writer API. The dependency remains pinned to a concrete commit, not a floating branch.

Commands run:
- `git -C deps/tracktion_engine fetch --depth 1 origin develop`
- `git -C deps/tracktion_engine checkout 2877b621f2fbee564d0696a616b86bf8ba8c8ab0`
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer`
- Attempted UI automation for Start/Stop with `osascript`, but macOS denied assistive access.

Result:
- Configure succeeded.
- Build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Launch smoke test succeeded.
- Runtime Tracktion initialization succeeded. Console output included:
  - `Audio block size: 512  Rate: 48000`
  - `Edit loaded in: 3 ms`
  - `[info] Playback engine initialized: Tracktion Engine v3.1.0`

Known issues:
- Automated Start/Stop button clicking was blocked by macOS assistive-access permissions for `osascript`, so Start/Stop UI clicks were not mechanically verified in this pass.
- Build emits dependency warnings from Tracktion/JUCE and one harmless linker warning about duplicate `tsq_core` static-library linkage.
- The engine status area was implemented, but no screenshot-based visual verification was captured in this pass.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 05 - Add Audio Device Settings Panel

Summary:
- Added versioned app settings support with `src/app/AppSettings.h/.cpp` and `src/app/AppSettingsService.h/.cpp`.
- App settings are stored separately from future `.tseq` project data at the platform app-data path. On this macOS machine, the file is:
  - `/Users/dawneweisman/Library/Application Support/TheorySequencer/app-settings.xml`
- Extended the TheorySequencer-owned `PlaybackEngine` abstraction with plain value types for audio output devices and device settings.
- Implemented Tracktion-backed device enumeration, current device reporting, output-device selection, and device settings restore/save helpers inside `TracktionPlaybackEngine`.
- Added `src/ui/AudioSettingsComponent.h/.cpp` and wired it into `MainComponent` behind an `Audio` button in the engine area.
- The audio settings panel shows the current output device, sample rate, buffer size, and available output-device choices.
- Persisted output device type/name, sample rate, and buffer size through the app settings service. JUCE returned an empty `createStateXml()` result for the default device selection on this machine, so restore falls back to the saved output type/name when no JUCE device-state XML is available.
- Kept settings IO in the app/UI workflow; no settings file access was added to the audio thread.

Commands run:
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer`
- Used `osascript` to open the `Audio` panel and click `Apply` for the current output device.
- Captured a visual smoke-test screenshot at `/tmp/theorysequencer-prompt05-audio-settings-final.png`.

Result:
- Configure succeeded.
- Build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Launch smoke test succeeded.
- Audio settings panel opened successfully and displayed:
  - Current output: `CoreAudio - Built-in Output`
  - Sample rate: `48000 Hz`
  - Buffer size: `512 samples`
- Clicking `Apply` saved `app-settings.xml`.
- Restart smoke test restored the saved output choice and logged:
  - `[info] Audio output restored: CoreAudio: Built-in Output`

Known issues:
- JUCE did not produce a non-empty audio device state XML blob for the default output selection in this environment; the app settings fallback by output type/name restored correctly.
- Build still emits dependency warnings from Tracktion/JUCE and the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 06 - Add VST3 Plugin Scanning Service

Summary:
- Added `src/engine/plugins/PluginDescription.h` as a TheorySequencer-owned plugin metadata type.
- Added `src/engine/plugins/PluginRegistry.h/.cpp` for an app-level VST3 metadata cache.
- Added `src/engine/plugins/PluginScanService.h/.cpp` using JUCE `VST3PluginFormat`, `KnownPluginList`, and `PluginDirectoryScanner`.
- Plugin scanning runs on a worker thread and is triggered explicitly from the UI; it does not run on the audio thread.
- Added a dead-man's-pedal file beside the plugin registry cache for JUCE scanner crash/failure tracking.
- Wired `PluginRegistry` and `PluginScanService` into `AppServices`.
- Added `src/ui/PluginBrowserComponent.h/.cpp` and a `Plugins` button in the main engine area.
- The Plugin Browser can open, trigger a VST3 scan, show scan status/search paths, and list cached VST3 metadata including name, manufacturer, format, instrument flag, and file identifier.
- No plugin loading, track assignment, audio effects workflow, AU, VST2, CLAP, AAX, or project-model changes were added.

Platform-specific scan/cache paths:
- VST3 search paths on this macOS machine:
  - `/Users/dawneweisman/Library/Audio/Plug-Ins/VST3`
  - `/Library/Audio/Plug-Ins/VST3`
- Plugin registry cache:
  - `/Users/dawneweisman/Library/Application Support/TheorySequencer/plugin-registry.xml`
- Scanner dead-man's-pedal file:
  - `/Users/dawneweisman/Library/Application Support/TheorySequencer/vst3-scan-dead-mans-pedal.txt`

Commands run:
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `find "/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/VST3" -maxdepth 3 -name '*.vst3' -print`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer`
- Used `osascript` to open the Plugin Browser and trigger `Scan VST3`.
- Captured a visual smoke-test screenshot at `/tmp/theorysequencer-prompt06-plugin-browser.png`.

Result:
- Configure succeeded.
- Build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded.
- Plugin Browser opened successfully.
- Real VST3 scan completed and wrote 6 cached plugin descriptions:
  - `AlterEgo` - instrument
  - `CaptainKaleidoscope` - not marked as instrument
  - `SpectralSynth` from `/Library/Audio/Plug-Ins/VST3` - instrument
  - `SpectralSynth` from user VST3 folder - instrument
  - `Synthesizer` - instrument
  - `TAL-Chorus-LX` - not marked as instrument
- Restart smoke test loaded the persisted cache and logged:
  - `[info] Plugin registry loaded: 6 entries`

Known issues:
- JUCE emitted debug assertions from `juce_VST3PluginFormatImpl.h:298` while probing the third-party `AlterEgo.vst3`; scanning continued, the app did not crash, and the registry cache was written.
- Build still emits dependency warnings from Tracktion/JUCE and the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 07 - Load One VST3 Instrument and Play Hardcoded MIDI

Summary:
- Extended the TheorySequencer-owned `PlaybackEngine` abstraction with a temporary test-instrument slot API:
  - load a selected VST3 instrument
  - play a hardcoded MIDI phrase
  - stop the phrase
  - open the loaded plugin editor when supported
  - report test-instrument status without exposing Tracktion types outside `/engine`
- Implemented the Tracktion-backed test slot inside `TracktionPlaybackEngine`.
- Initialised Tracktion's plugin manager before external plugin creation so JUCE VST3 formats are available to Tracktion's plugin cache.
- Added a stale-cache fallback that re-reads a selected VST3 file with `juce::VST3PluginFormat` when existing cached metadata lacks JUCE numeric plugin IDs.
- Created a real one-bar MIDI clip in the single Tracktion edit at 120 BPM with quarter notes:
  - C4, E4, G4, C5
- Added Plugin Browser controls:
  - `Load Selected Instrument`
  - `Play Test Phrase`
  - `Stop`
  - `Open Editor`
  - loaded plugin name/status display
- Persisted the selected test instrument identifier/name in app settings:
  - `/Users/dawneweisman/Library/Application Support/TheorySequencer/app-settings.xml`
- Kept plugin hosting inside `/engine`; no full project model, clips timeline, piano roll, effects workflow, or multi-track implementation was added.

Manual plugin tested:
- OS: macOS
- Audio output: `CoreAudio - Built-in Output`, `48000 Hz`, `512 samples`
- VST3 instrument: `SpectralSynth`
- VST3 path: `/Library/Audio/Plug-Ins/VST3/SpectralSynth.vst3`
- Persisted identifier: `VST3-SpectralSynth-ce8e9321-bcd43955`

Commands run:
- `cmake --build build --target tsq_engine`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- Used `osascript` to launch the app, open Plugin Browser, select `SpectralSynth`, click `Load Selected Instrument`, click `Play Test Phrase`, click `Stop`, and open the plugin editor.
- Captured visual smoke-test screenshots:
  - `/tmp/theorysequencer-prompt07-loaded-play-stopped.png`
  - `/tmp/theorysequencer-prompt07-plugin-editor.png`

Result:
- Build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded.
- Tracktion Engine initialized and restored the CoreAudio Built-in Output device.
- Plugin Browser loaded the persisted VST3 cache.
- `SpectralSynth` loaded into the test instrument slot.
- The test phrase transport path started and stopped through the Prompt 07 controls.
- The Plugin Browser displayed `Loaded: SpectralSynth` and `Stopped test phrase`.
- The plugin editor opened as a second window named `SpectralSynth`.
- App settings persisted:
  - `<TestInstrument identifier="VST3-SpectralSynth-ce8e9321-bcd43955" name="SpectralSynth"/>`
- App shutdown completed after closing the plugin editor/app; no persistent `TheorySequencer` process remained.

Known issues:
- I could verify the load/play/stop UI path and Tracktion transport state, but cannot literally audit audible speaker output from this tool environment.
- The existing VST3 cache from Prompt 06 did not contain `uniqueId`/`deprecatedUid` attributes; Prompt 07 now resolves fresh VST3 metadata during load when those fields are absent, and future scans will save the IDs.
- The first load attempts against stale metadata failed with `Unable to load VST-3 plug-in file`; this was resolved by initializing Tracktion's plugin manager and refreshing VST3 metadata at load time.
- Build still emits dependency warnings from Tracktion/JUCE and the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 08 - Implement Core Time Model

Summary:
- Added the JUCE/Tracktion-independent core musical time model under `src/core/time`.
- Added strong tick domain types:
  - `TickPosition`
  - `TickDuration`
  - fixed `ticksPerQuarterNote = 960`
- Added standard tick duration helpers for quarter, eighth, and sixteenth notes.
- Added musical bar/beat position value types.
- Added validated `Tempo` and `TimeSignature` primitives.
- Added `TempoMap` with tempo nodes, BPM lookup, tick/second conversion, and exact integration for linear BPM interpolation.
- Added `TimeSignatureMap` with marker changes, bar-length lookup, bar/beat-to-tick conversion, and tick-to-bar/beat conversion.
- Added `GridDivision` support for standard note-value divisions.
- Added `Tuplet` support with total duration, exact per-note tick duration, and rounded tick duration.
- Added `tests/core/TimeModelTest.cpp` covering the Prompt 08 acceptance cases.
- Kept `tsq_core` independent from JUCE and Tracktion; the core target still has no JUCE/Tracktion link dependencies.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `cmake --build build --target tsq_core_tests && ctest --test-dir build --output-on-failure`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build && ctest --test-dir build --output-on-failure`

Result:
- Core test target built successfully.
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Time model tests passed for:
  - PPQ constant
  - quarter/eighth/sixteenth durations
  - standard grid divisions
  - 4/4, 3/4, and 7/8 bar lengths
  - fixed-tempo tick/second conversion
  - 120-to-140 BPM linear tempo interpolation
  - time-signature marker changes
  - quarter-note quintuplet duration

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 09 - Implement Pitch, Note Spelling, and Enharmonic Core

Summary:
- Added the JUCE/Tracktion-independent pitch and spelling model under `src/core/music_theory`.
- Added `PitchClass` as a normalized 12-tone identity type with chromatic transposition.
- Added `NoteName`, `LetterName`, and `Accidental` so displayed spelling remains distinct from pitch-class identity.
- Added practical enharmonic helpers:
  - sharp/flat spelling preference
  - common spellings for a pitch class
- Added `MidiPitch` as a separate absolute MIDI pitch type with validation, octave helpers, pitch-class lookup, and display-name helpers such as `C4`, `Bb3`, and `F#5`.
- Added `ScaleDegree` for natural and altered diatonic degrees such as `1`, `b3`, `#4`, and `b7`.
- Added `tests/core/PitchModelTest.cpp` covering the Prompt 09 acceptance cases.
- Kept `tsq_core` independent from JUCE and Tracktion; no UI, scale library, or chord-recognition work was added.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure && cmake --build build`

Result:
- Core test target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Pitch model tests passed for:
  - C# and Db mapping to the same pitch class while retaining different spellings
  - MIDI 60 displaying as `C4`
  - alternate display helpers including `Bb3`, `F#5`, and flat-preferred `Db4`
  - chromatic pitch-class transposition
  - MIDI pitch validation
  - scale-degree natural/flat/sharp string forms

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- Practical spelling is intentionally simple until Prompt 10 introduces scale/mode context.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 10 - Implement Scale and Mode Library Core

Summary:
- Added the JUCE/Tracktion-independent scale and mode model under `src/core/music_theory`.
- Added `ScaleMetadata` for name, category, tags, and description.
- Added `ScaleDefinition` as a root-independent scale definition with pitch-class offsets, scale-degree mapping, and preferred spelling behavior.
- Added `ScaleInstance` to combine a `NoteName` key center/root with a `ScaleDefinition`.
- Added context-aware visible note spelling from scale-degree letters, so examples like F Major produce `Bb` and E Major produces sharps.
- Added `ScaleLibrary` with a built-in starter library:
  - Chromatic
  - Major / Ionian
  - Natural Minor / Aeolian
  - Dorian
  - Phrygian
  - Lydian
  - Mixolydian
  - Locrian
  - Harmonic Minor
  - Melodic Minor
  - Major Pentatonic
  - Minor Pentatonic
  - Major Blues
  - Minor Blues
  - Whole Tone
  - Diminished Half-Whole
  - Diminished Whole-Half
- Added lookup by name or exact alias tag, text search over metadata, scale instantiation, pitch-class membership checks, and visible spelling generation.
- Added `tests/core/ScaleLibraryTest.cpp` covering the Prompt 10 acceptance cases.
- Kept all new work inside `tsq_core`; no UI palette or custom scale editor was added.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`

Result:
- Core test target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Scale library tests passed for:
  - C Major = `C D E F G A B`
  - C Mixolydian = `C D E F G A Bb`
  - F Major using `Bb`, not `A#`
  - E Major using `F#`, `G#`, `C#`, and `D#`
  - D Dorian = `D E F G A B C`
  - Search text `mix` finding Mixolydian
  - Chromatic including all 12 pitch classes
  - Ionian alias lookup resolving to Major

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- The starter library is intentionally built-in C++ data for now; Prompt 11 may extend the model for user-defined custom scales.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 11 - Implement Custom Scale Model

Summary:
- Added the JUCE/Tracktion-independent custom scale model under `src/core/music_theory`.
- Added `CustomScaleSpecification` to capture custom scale metadata and selected C-based pitch classes from the visual keyboard model.
- Added `CustomScaleValidationResult` and validation issue codes for:
  - empty names
  - missing root C
  - duplicate pitch classes
- Chose an explicit missing-C rule: custom scale definitions are rejected if C is not selected, rather than silently corrected.
- Added `CustomScaleBuilder` to convert selected C-keyboard pitch classes into a shared `ScaleDefinition`.
- Custom scale offsets are normalized into chromatic order from C, and degree mappings use the C-keyboard sharp-oriented spelling model.
- Added `ScaleLibrary::addDefinition` so custom scales and built-ins share the same lookup, instantiation, and metadata search behavior.
- Added `tests/core/CustomScaleBuilderTest.cpp` covering the Prompt 11 acceptance cases.
- No UI editor or serialization work was added.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`

Result:
- Core test target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Custom scale tests passed for:
  - C D E F G A B creating a Major-like pitch collection
  - C D E F# G# A# creating a Whole Tone-like collection
  - missing C being rejected by validation and builder
  - empty name and duplicate pitch-class validation
  - metadata preservation
  - adding a custom scale to `ScaleLibrary` and finding it via search

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- One-note custom scales containing only C are allowed by validation, matching Prompt 11's note that an additional note is preferred but not strictly required.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 12 - Implement Harmonic Context Lanes Core

Summary:
- Added the JUCE/Tracktion-independent sequencing harmonic structure model under `src/core/sequencing`.
- Added `Region` as a half-open `[start, end)` tick interval with negative-duration validation.
- Added `KeyCenterRegion` for start/end ticks plus pitch class.
- Added `ScaleModeRegion` for start/end ticks plus scale definition name.
- Added `ChordRegion` as a small placeholder carrying a chord name over a tick region; no chord analysis or Roman numeral work was added.
- Added `MusicalStructure` to own song-level key center, scale/mode, and chord lanes.
- Documented in code behavior through the model: gaps resolve to the project default harmonic context, which is C Major by default.
- Added `HarmonicContext` for active key center plus active scale/mode, with `scaleInstance()` support through `ScaleLibrary`.
- Added `HarmonicContextResolver` for resolving a single tick and splitting a clip/range into harmonic context segments.
- No UI code and no automatic MIDI note transformation were added.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`

Result:
- Core test target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Harmonic context tests passed for:
  - default C Major at bar 1
  - C Mixolydian after a scale region change
  - independent key center and scale/mode changes
  - a clip/range spanning bars 1-4 C Major and bars 5-8 C Natural Minor
  - deterministic half-open boundary behavior
  - rejection of negative-duration regions

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- Region overlap policy is deterministic but permissive for now; the most recent containing region with the latest start wins. Prompt 13 and command-layer work can add stricter edit validation when project mutation rules arrive.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 13 - Implement Sequencer Project Model

Summary:
- Added the JUCE/Tracktion-independent sequencer project model under `src/core/sequencing`.
- Added `Project` to own unlimited-by-design MIDI tracks and the song-level `MusicalStructure`.
- Added `Track` to own MIDI clips and enforce the same-track no-overlap rule.
- Added `MidiClip` with:
  - ID
  - name
  - project start tick
  - project length duration
  - loop settings
  - clip-local MIDI notes
  - harmonic metadata placeholder
- Added `MidiNote` with:
  - ID
  - MIDI pitch
  - clip-local start tick
  - duration
  - velocity
  - optional spelling metadata
- Added `ClipLoop` to represent disabled or enabled loop state and calculate clip-local loop repetition regions.
- Added `ClipOverlap` helper for deterministic half-open clip overlap checks.
- Clip-local note timing and project timeline placement are kept separate via explicit conversion helpers.
- No UI, playback sync, MIDI export, commands, or serialization were added.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`

Result:
- Core test target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Project model tests passed for:
  - adding multiple tracks
  - adding non-overlapping clips
  - rejecting overlapping clips on the same track
  - allowing same-time clips on different tracks
  - adding clip-local MIDI notes
  - rejecting invalid note durations
  - clip local-to-project and project-to-local tick conversion
  - calculating loop repetitions

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- IDs are caller-provided strings for now; command/model creation policy can introduce stronger ID generation later.
- Harmonic metadata is intentionally only a placeholder until Prompt 14.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 14 - Implement Clip Harmonic Metadata and Accidental Rules

Summary:
- Added `ClipHarmonicMap` for per-clip harmonic metadata across clip-local time.
- Added `ClipHarmonicRegion` entries containing:
  - clip-local start/end tick region
  - original key center
  - original scale/mode name via `HarmonicContext`
- Added `ClipHarmonicMap::snapshotFromProject` and `MidiClip::snapshotHarmonicContext` so clips can capture project harmonic context across their duration.
- Extended `MidiClip` to own a real `ClipHarmonicMap` instead of the previous placeholder metadata vector.
- Extended `HarmonicContext` with pitch-class containment and equality helpers.
- Added `AccidentalVisibility` pure functions for:
  - native scale lanes
  - used accidental lanes
  - chromatic reveal lanes
  - complete visible lanes for a clip/time area
- Added lane status values:
  - `nativeScale`
  - `usedAccidental`
  - `chromaticReveal`
- Documented rules in code behavior:
  - default diatonic view shows native scale lanes plus used accidental lanes
  - used accidentals are per clip and appear only when that clip has an out-of-scale note in the queried time/context
  - chromatic reveal shows all 12 pitch classes
  - chromatic reveal keeps native lanes normal and marks unused non-scale lanes greyed/editable
  - no automatic pitch changes are performed
- Added `tests/core/AccidentalVisibilityTest.cpp` covering Prompt 14 acceptance cases.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`

Result:
- Core test target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Accidental visibility tests passed for:
  - C Major showing C D E F G A B by default
  - an F# note in a C Major clip making F# visible as a used accidental
  - a different clip without F# not showing F#
  - chromatic reveal showing all 12 pitch classes
  - chromatic reveal marking unused non-scale lanes greyed/editable
  - chromatic reveal preserving used accidental status
  - a clip spanning C Major then C Mixolydian resolving B/Bb visibility dynamically

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- Lane visibility is pure core calculation only; no UI rendering was added.
- Metadata snapshotting is available on the model, but command-layer creation/edit hooks will arrive in later prompts.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 15 - Implement Command and Undo/Redo Framework

Summary:
- Added the JUCE/Tracktion-independent command framework under `src/core/commands`.
- Added `Command` and `CommandResult` for project mutations with execute/undo and clear failure messages.
- Added `ProjectCommandContext` to provide commands access to the core `Project` model.
- Added `CommandStack` with undo and redo stacks.
- `CommandStack` behavior:
  - successful execute pushes to undo stack
  - failed execute does not mutate stack state
  - undo moves commands to the redo stack
  - redo re-executes commands and returns them to undo stack
  - executing a new command clears redo history
- Added concrete project mutation commands:
  - `AddTrackCommand`
  - `AddClipCommand`
  - `MoveClipCommand`
  - `ResizeClipCommand`
  - `DeleteClipCommand`
  - `AddNoteCommand`
  - `MoveNoteCommand`
  - `ResizeNoteCommand`
  - `DeleteNoteCommand`
  - `AddKeyCenterRegionCommand`
  - `AddScaleModeRegionCommand`
- Extended the core sequencing model with narrow mutation helpers needed by commands:
  - remove tracks by ID
  - remove, move, and resize clips
  - remove, move, and resize notes
  - exact removal of key center, scale/mode, and chord regions
- Clip overlap enforcement remains in the model and is exercised through commands.
- No UI code and no chord commands were added.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`

Result:
- Core test target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Command framework tests passed for:
  - add note undo/redo
  - move note undo/redo
  - add clip undo/redo
  - move clip failing clearly when it would overlap another same-track clip
  - add scale/mode region undo/redo
  - redo stack clearing after a new command

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- Commands operate on caller-provided IDs; stronger ID generation remains a future project-model or command-policy concern.
- UI integration is intentionally absent until later UI prompts.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 16 - Implement Project Serialization and .tseq Package

Summary:
- Added core serialization support under `src/core/serialization`.
- Added `ProjectSchemaVersion.h` with schema version 1 from day one.
- Added `ProjectMigration` scaffolding; v1 is currently the only supported schema and unsupported versions return clear errors.
- Added a small core-owned structured JSON value/parser/writer in `JsonHelpers` to keep `tsq_core` independent from JUCE and UI/backend dependencies.
- Added `ProjectSerializer` for project.json round-trip serialization.
- Added `ProjectPackage` for `.tseq` package folder save/load.
- Package save creates:
  - `project.json`
  - `plugin-states/`
  - `assets/`
  - `exports/`
- Extended `Project` to store custom scale definitions.
- Extended `MidiClip` with `setHarmonicMetadata` so deserialization can restore clip-local harmonic metadata.
- `project.json` v1 includes:
  - `schemaVersion`
  - project ID/name metadata
  - tracks
  - clips
  - notes
  - clip loop settings
  - clip harmonic metadata
  - musical structure lanes
  - custom scales
  - rhythmic settings placeholder
  - plugin reference placeholders
- Plugin state blobs are not embedded in `project.json`.
- No UI save/load integration and no plugin state persistence were added.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`

Result:
- Core test target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Serialization tests passed for:
  - empty project round trip
  - project with tracks, clips, notes, and loop settings round trip
  - musical structure lane round trip
  - custom scale round trip
  - invalid schema version error
  - missing required field error
  - `.tseq` package folder creation and load

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- The JSON helper intentionally supports the JSON features needed by project files today; future schema complexity may justify replacing it with a pinned core-safe JSON dependency.
- Rhythmic settings and plugin references are placeholders only, as requested by Prompt 16.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 17 - Implement Standard MIDI Export

Summary:
- Added JUCE/Tracktion-independent Standard MIDI File export under `src/core/midi`.
- Added `MidiExportOptions` with:
  - MIDI channel
  - tempo
  - time signature
  - tempo meta-event toggle
  - time-signature meta-event toggle
- Added `MidiFileWriter`, a small isolated SMF writer for format-0 MIDI files.
- Added `MidiExporter` for:
  - exporting a selected `MidiClip` to bytes
  - writing a selected `MidiClip` to a `.mid` file
- Export uses 960 PPQ from the core time model.
- Export writes:
  - `MThd` header
  - one `MTrk` track chunk
  - optional tempo event at tick 0
  - optional time signature event at tick 0
  - note-on and note-off events using clip-local note timing
  - end-of-track event
- MIDI pitch and velocity are preserved.
- Scale, chord, harmonic, and TheorySequencer metadata are intentionally not exported into the Standard MIDI File.
- No UI export, audio export, or MusicXML work was added.

Commands run:
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`

Result:
- Core test target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- MIDI export tests passed for:
  - format-0 Standard MIDI header
  - 960 PPQ division
  - C E G note-on events
  - note-off timing and note durations
  - pitch and velocity preservation
  - tempo meta event
  - time signature meta event
  - empty clip export
  - writing a `.mid` file to disk

Phase Gate 2 status:
- Core tests pass.
- Time model works.
- Scale library works.
- Harmonic context resolver works.
- Project/track/clip/note model works.
- Undo/redo works.
- `.tseq` serialization works.
- MIDI export works.

Known issues:
- The full build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage.
- Tempo and time signature export currently uses the single values supplied in `MidiExportOptions`; project-level tempo/time-signature lanes are not yet attached to the `Project` model.
- Manual DAW import was not performed in this automated pass, but the generated file is a valid SMF byte stream covered by parser-style tests.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 18 - Replace Placeholder UI With Product Layout Shell

Summary:
- Replaced the centered placeholder UI with a product-oriented JUCE layout shell.
- Added separate rendering components under `src/ui`:
  - `TransportComponent`
  - `HarmonicHeaderComponent`
  - `TrackListComponent`
  - `TimelineComponent`
  - `InspectorComponent`
  - `DetailEditorComponent`
  - `StatusBarComponent`
- `AppServices` now owns a default in-memory project and command stack.
- The default project is initialized with:
  - 120 BPM app default tempo
  - 4/4 app default time signature
  - C Major default harmonic context from `MusicalStructure`
  - one MIDI track, created through `AddTrackCommand`
- The central harmonic header resolves the current harmonic context at playhead tick 0 using `HarmonicContextResolver`.
- The main shell now includes:
  - top transport/header controls
  - large central harmonic display showing `C Major`
  - left track list area
  - main timeline area with structure lane shell
  - lower detail editor placeholder
  - right inspector placeholder
  - bottom status bar
- Existing audio settings and plugin browser panels remain reachable from transport buttons as temporary overlay panels.
- UI reads project state from app services; no timeline editing, piano roll, or chord lane work was added.

Commands run:
- `cmake --build build --target tsq_app`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`
- `open -n build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app` followed by AppleScript quit

Result:
- App target built successfully.
- Built app launched and quit cleanly.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Layout shell build verifies:
  - separate UI components compile and link
  - app services project wiring compiles
  - harmonic display resolver wiring compiles
  - existing core tests still pass

Known issues:
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Timeline, detail editor, and inspector are non-editing shell surfaces until later prompts.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 19 - Timeline UI With Tracks and Non-Overlapping MIDI Clips

Summary:
- Implemented the first interactive timeline editor in `TimelineComponent`.
- The timeline now displays:
  - bar ruler with beat grid lines
  - musical structure shell lanes
  - multiple MIDI track rows
  - MIDI clips as rectangular timeline blocks
  - selected clip and invalid-overlap visual states
- Added a `+ Track` button that creates MIDI tracks through `AddTrackCommand`.
- Added double-click clip creation on a track through `AddClipCommand`.
- Added horizontal clip drag through `MoveClipCommand`.
- Added right-edge clip resizing through `ResizeClipCommand`.
- Added selected clip deletion through `DeleteClipCommand`.
- Timeline editing uses ticks internally and snaps to quarter-note ticks.
- Same-track clip overlap is previewed as invalid during drag/resize and remains enforced by the core `Track` model and command execution.
- Clips at the same time on different tracks remain allowed because overlap validation is track-local.
- Added Cmd/Ctrl+Z and redo handling for timeline edits.
- Cleared the startup default-track command history after app bootstrap so the first user undo does not remove the default project track.
- Main UI now periodically repaints project-reading panels so track/clip edits appear across timeline, track list, inspector, and status surfaces.

Commands run:
- `cmake --build build --target tsq_app`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `open -n build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app` followed by AppleScript quit

Result:
- `tsq_app` built successfully.
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Built app launched and quit cleanly.
- Prompt 19 behavior compiles against existing command/model boundaries:
  - timeline mutations go through command objects
  - core project/track/clip model remains the source of truth
  - UI does not directly mutate project state

Known issues:
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Manual drag/drop interaction was smoke-tested by launch only in this automated pass; detailed UI gesture verification should be done interactively.
- Timeline has no horizontal/vertical scrolling yet, so very long arrangements or many tracks can extend beyond the visible surface.
- Clip creation currently uses a fixed 4-bar default length and quarter-note snapping.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 20 - Add Ableton-Style MIDI Clip Looping UI

Summary:
- Added explicit source-versus-arrangement semantics to `MidiClip`:
  - `length()` remains the visible arrangement length.
  - `sourceLength()` returns the editable MIDI source length.
  - looped clips derive source length from `ClipLoop::loopDuration()`.
  - unlooped clips use visible length as source length.
- Added `MidiClip::withLoop()` and updated note validation so looped clips keep one source note list instead of duplicating notes for repetitions.
- Added `Track::replaceClip()` so commands can atomically replace clip loop/length state while preserving same-track overlap validation.
- Extended `ResizeClipCommand` with an optional `ClipLoop` argument.
- Timeline right-edge drag now supports loop extension:
  - extending an unlooped clip enables looping with the original clip length as source length
  - resizing an already looped clip changes visible arrangement length while preserving source length
  - shortening a looped clip keeps source note data intact
- Timeline clip drawing now shows loop repetitions with internal separators and subtle alternating repetition shading.
- Existing serialization already stored visible `lengthTicks` and loop `durationTicks`; tests now assert the round-tripped source length.
- Added tests for:
  - source length separate from visible arrangement length
  - loop note validation against source length
  - shortening visible loop length without losing source notes
  - command undo/redo for loop extension
  - command shortening of looped visible arrangement length
  - serialization round trip of loop source length

Commands run:
- `cmake --build build --target tsq_core_tests`
- `cmake --build build --target tsq_app`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`
- `open -n build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app` followed by AppleScript quit

Result:
- `tsq_core_tests` built successfully.
- `tsq_app` built successfully.
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Built app launched and quit cleanly.
- Prompt 20 behavior keeps loop repetitions represented in the model without duplicating MIDI notes.

Known issues:
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Manual loop-edge dragging was smoke-tested by build/launch only in this automated pass; detailed UI gesture verification should be done interactively.
- Timeline has no horizontal/vertical scrolling yet, so long looped arrangements can extend beyond the visible surface.
- There is not yet a dedicated loop brace editor inside the piano roll; that is explicitly deferred by Prompt 20.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 21 - Implement Musical Structure Lanes UI

Summary:
- Added project-owned tempo and time-signature maps:
  - `Project::tempoMap()`
  - `Project::timeSignatureMap()`
- Added undoable commands for structure lane edits:
  - `AddTempoNodeCommand`
  - `AddTimeSignatureMarkerCommand`
  - `ReplaceKeyCenterRegionCommand`
  - `ReplaceScaleModeRegionCommand`
- Extended project serialization to write/read:
  - `tempoMap.nodes`
  - `timeSignatureMap.markers`
- Serialization remains backward-tolerant for project JSON that does not yet contain those fields, using default 120 BPM and 4/4 maps.
- Replaced the timeline's placeholder structure strip with four editable lanes:
  - Tempo
  - Meter
  - Key
  - Scale
- Tempo lane shows tempo nodes and a simple interpolation line between nodes.
- Time signature lane shows meter markers.
- Key center and scale/mode lanes show brace-style regions.
- Double-click lane editing now supports:
  - adding/editing tempo BPM nodes
  - adding/editing time signature markers
  - adding/editing key center regions from 12 pitch classes
  - adding/editing scale/mode regions from built-in and project custom scales
- Key and scale regions can be moved or resized by dragging the region body or brace handles.
- Timeline ruler clicks update a UI playhead tick.
- `HarmonicHeaderComponent` now reads tempo and meter from the project maps at the current playhead tick.
- Central harmonic display updates when the timeline playhead is moved into a different key/scale region.
- Added tests for:
  - tempo node command undo/redo
  - time signature marker command undo/redo
  - key/scale region replacement commands
  - tempo and time-signature serialization round trip

Commands run:
- `cmake --build build --target tsq_app`
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`
- `open -n build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app` followed by AppleScript quit

Result:
- `tsq_app` built successfully.
- `tsq_core_tests` built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Built app launched and quit cleanly.
- Structure lane data now lives in the project model and is edited through commands.

Known issues:
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Detailed manual verification of lane dialogs and key/scale drag handles should be done interactively; this automated pass verified build/test/launch only.
- Tempo editing is node-based with a simple line visualization, not a full automation editor.
- Time signature editing is marker-based and does not yet provide advanced bar-aware lane constraints.
- Timeline still has no horizontal/vertical scrolling, so long arrangements and many tracks/regions can extend beyond the visible surface.
- Chord progression lane, scale palette drag/drop, and piano roll remain deferred to later prompts.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 22 - Scale Palette UI and Drag-to-Scale-Lane

Summary:
- Added `ScalePaletteComponent` as the right-side panel.
- The scale palette includes:
  - search field
  - grouped scale sections
  - built-in scale rows with name and description/formula fallback
  - custom scale rows from the current project
  - `New` button for custom scale creation
- Grouping currently maps the existing scale library into:
  - Church Modes
  - Major Scales
  - Minor Scales
  - Pentatonic
  - Blues
  - Jazz
  - Synthetic
  - Custom
- Added drag support from palette rows to the timeline scale/mode lane.
- `TimelineComponent` is now a JUCE drag-and-drop target for scale palette payloads.
- Dropping a scale on an existing scale/mode region replaces that region's scale name through `ReplaceScaleModeRegionCommand`.
- Dropping a scale on empty scale-lane space creates a 4-bar scale/mode region through `AddScaleModeRegionCommand`.
- Added scale-lane drop preview rendering.
- Added custom scale editor modal:
  - name
  - category
  - tags
  - description
  - one-octave C-based toggle keyboard: C C# D D# E F F# G G# A A# B
- Custom scale saves go through the new `AddCustomScaleCommand`.
- Added `Project::removeCustomScaleByName()` so custom scale addition can be undone.
- Existing custom scale serialization remains in place; custom scales saved by the editor are stored in the project model and already covered by `.tseq` round-trip tests.
- Added a command test for custom scale add/undo/redo.
- `MainComponent` now acts as a JUCE `DragAndDropContainer` so palette rows can initiate drag operations.

Commands run:
- `cmake --build build --target tsq_app`
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build build`
- `open -n build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app` followed by AppleScript quit

Result:
- `tsq_app` built successfully.
- `tsq_core_tests` built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- Full build succeeded.
- Built app launched and quit cleanly.
- Scale palette and custom scale save paths are command-backed and compile against the current project model.

Known issues:
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Detailed manual verification of palette drag/drop and the custom scale modal should be done interactively; this automated pass verified build/test/launch only.
- The palette uses the right-side panel for now, replacing the placeholder inspector surface.
- Category grouping is derived from the current starter scale library metadata and names; it can become more explicit as the library grows.
- Scale palette drag/drop only targets the scale/mode lane; chord lane and piano roll remain deferred.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 23 - Open MIDI Clip in Piano Roll Editor

Summary:
- Added `PianoRollComponent` as the first read-only piano roll surface.
- Double-clicking an existing timeline MIDI clip now selects it and opens it in the lower/detail editor area.
- Replaced the placeholder detail editor with a piano roll host while keeping the editor read-only for this prompt.
- The piano roll reads the selected `MidiClip` from the project model and resolves harmonic context from the project musical structure.
- The roll displays:
  - clip name and current harmonic context
  - chromatic pitch lanes from MIDI 127 to 0
  - clip-local bar/beat grid based on 960 PPQ and 4/4 starter bars
  - existing MIDI notes as pitch/time rectangles
  - horizontal and vertical scrollbars through a JUCE viewport
- No piano roll editing, scale filtering, chord overlays, velocity lane, or direct UI mutation was added.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.
- Timeline-to-detail-editor clip opening path compiles and is wired through `TimelineComponent::onClipOpened`.

Known issues:
- Detailed manual verification of the double-click gesture and note rendering should be done interactively; this automated pass verified build/test/launch only.
- Piano roll zoom is fixed for now.
- The grid assumes the current starter 4/4 bar layout; later prompts can make it fully meter-aware.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 24 - Piano Roll Note Editing

Summary:
- Added basic command-backed note editing to `PianoRollComponent`.
- Piano roll editing now supports:
  - clicking an editable lane to create a note
  - selecting notes
  - Shift/Cmd/Ctrl multi-select toggling
  - dragging selected notes in time and pitch
  - resizing note start or end with edge handles
  - deleting selected notes with Delete/Backspace
  - duplicating selected notes with Cmd/Ctrl+D when the duplicate fits inside the clip source
- Note creation uses clip-local ticks, sixteenth-note snapping, quarter-note default duration, default velocity 100, and pitch spelling metadata from the chromatic spelling helper.
- All piano roll mutations go through the command stack using note commands; no direct UI mutation of the project model was added.
- Extended core note editing support:
  - `MidiNote::withPitch()`
  - `MidiClip::replaceNote()`
  - `MoveNoteCommand` can optionally move pitch with time
  - `ResizeNoteCommand` can optionally resize from the start edge by changing start and duration together
- Added command tests for pitch-moving notes, start-edge note resizing, and delete-note undo/redo.
- Existing serialization and MIDI exporter tests continue to cover note persistence and MIDI note export through the project model.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Detailed manual verification of note creation, multi-select, dragging, resizing, deletion, and duplication should be done interactively; this automated pass verified build/test/launch only.
- Multi-note edits execute one command per note, so undo currently walks those edits one note at a time rather than as one grouped transaction.
- Piano roll grid is fixed at sixteenth-note snap for this prompt; no grid selector or zoom-aware snapping was added.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 25 - Add Scale-Filtered Piano Roll Lanes

Summary:
- Updated `PianoRollComponent` to render native scale-filtered lanes instead of all 128 chromatic MIDI rows by default.
- The piano roll now resolves its visible lanes from the project harmonic context:
  - selected note timing when notes are selected
  - otherwise the current playhead position if it is inside the opened clip
  - otherwise the clip start position
- Threaded the project playhead into `DetailEditorComponent` and `PianoRollComponent` so the roll can update when the playhead enters a different key/scale region.
- Lane generation uses the current key center and scale/mode, including project custom scales when present.
- Context-aware lane and note labels now come from scale spelling:
  - C Mixolydian uses `Bb`
  - F Major uses `Bb`
  - E Major uses `F#`, `G#`, `C#`, and `D#`
- Note entry now creates notes only on visible scale lanes and stores the lane spelling metadata.
- Vertical note dragging now moves through visible scale lanes rather than chromatic semitone rows.
- Pitch-moving note commands can now receive optional spelling metadata, preserving context-aware note names when notes are moved.
- Added pure core test coverage for native lane spellings in F Major and E Major.
- Kept chromatic reveal, greyed non-scale lanes, and used accidental lane persistence deferred to Prompt 26.

Commands run:
- `cmake --build build`
- `cmake --build build && ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Detailed manual visual verification of C Major, C Mixolydian, and playhead-based piano-roll lane changes should be done interactively; this automated pass verified build/test/launch only.
- Existing out-of-scale notes are hidden in the piano roll until Prompt 26 adds chromatic reveal and per-clip accidental lanes.
- The piano roll still uses fixed zoom and fixed sixteenth-note snapping.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 26 - Add Chromatic Reveal and Per-Clip Accidental Lanes

Summary:
- Added a visible `Show Chromatic` toggle to the piano roll header.
- Added a piano-roll keyboard shortcut: `C` toggles chromatic reveal while the roll content has focus.
- When chromatic reveal is enabled, the piano roll shows all chromatic MIDI pitches across octaves.
- Native scale lanes keep normal styling.
- Non-scale chromatic-only lanes are greyed but remain editable.
- When chromatic reveal is disabled, the piano roll hides unused chromatic lanes but keeps used accidental lanes visible for the opened clip.
- Used accidental lanes are derived from the current clip's note contents and the active project harmonic context, so accidental visibility is per clip rather than per track or global.
- Removing the last note on an accidental pitch hides that accidental lane again when chromatic reveal is off.
- Note creation in greyed chromatic lanes works through the existing `AddNoteCommand`; after creation, that pitch becomes a used accidental lane for the clip.
- Updated core accidental-visibility tests with the removal case.
- Kept clip harmonic metadata snapshot/relocation behavior deferred to Prompt 27.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Detailed manual verification of the `Show Chromatic` toggle, `C` shortcut, greyed lane editing, and per-clip accidental visibility should be done interactively; this automated pass verified build/test/launch only.
- UI accidental visibility currently evaluates against the current project harmonic context and clip notes; full clip harmonic metadata snapshot/relocation behavior remains for Prompt 27.
- The piano roll still uses fixed zoom and fixed sixteenth-note snapping.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 27 - Clip Relocation Harmonic Metadata and Transpose Buttons

Summary:
- Added undoable explicit clip transpose commands:
  - `ChromaticTransposeClipCommand`
  - `ScaleDegreeTransposeClipCommand`
- `AddClipCommand` now snapshots clip harmonic metadata from the project musical structure when a clip is created without existing metadata.
- Clip movement remains non-destructive: `MoveClipCommand` relocates the clip without altering MIDI note pitches.
- Chromatic clip transpose uses the clip's stored source harmonic metadata and the requested target context key-center delta.
- Scale-degree clip transpose maps notes through the current theory model and preserves basic chromatic alterations.
- Added piano-roll buttons for explicit transpose actions:
  - `Chromatic Transpose`
  - `Scale-Degree Transpose`
- Transpose buttons execute commands through the command stack; the UI does not directly mutate project state.
- Added core tests for:
  - C Major then C Natural Minor metadata snapshot across an 8-bar clip
  - moving a clip without changing pitches
  - chromatic C Major to D Major triad transpose
  - scale-degree C Major to D Dorian triad transpose
  - raised fourth accidental preservation from C Major F# to D Major G#

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Detailed manual verification of the new piano-roll transpose buttons should be done interactively; automated coverage verifies command behavior and build integrity.
- The scale-degree transpose command implements well-tested basic cases from Prompt 27, not a complete future-proof melodic analysis system.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 28 - Sync Core Project MIDI Clips to Playback Engine

Summary:
- Added `TrackInstrumentReference` to the core track model so MIDI tracks can store one assigned VST3 instrument reference.
- Added undoable `AssignTrackInstrumentCommand`.
- Added command-stack change callbacks; successful project commands now mark playback state dirty through `AppServices`.
- Added track instrument serialization under each track's `pluginReference`.
- Added `PlaybackEngine::syncProject`.
- Implemented Tracktion project sync:
  - rebuilds a Tracktion edit from the core `Project`
  - creates one Tracktion audio track per core MIDI track
  - loads assigned VST3 instruments onto their tracks
  - creates Tracktion MIDI clips from core MIDI clips
  - expands looped core clips into prepared playback repetitions
  - schedules notes in beat space from core PPQ ticks
  - configures the Tracktion tempo sequence from the core tempo map's tempo nodes
- Updated transport Start to sync and play the current project model instead of starting stale/hardcoded state.
- Updated Plugin Browser so a scanned VST3 instrument can be assigned to a selected project track.
- Track list now shows the assigned instrument name or `No instrument`.
- Kept hardcoded test phrase controls as diagnostics only.
- Added core tests for track instrument command undo/redo and serialization persistence.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual verification with an actual VST3 instrument is still required for the full acceptance path: create clip, add notes, assign instrument, press Start, and hear playback.
- Project sync currently rebuilds the Tracktion edit on the message/control path when playback is dirty; this is intentionally off the audio thread but not yet optimized for large projects.
- Looped clips are expanded into prepared Tracktion MIDI clip repetitions during sync; the core model still stores looped clips non-destructively.
- The Prompt 29 transport/playhead/loop integration remains outstanding; Start/Stop works, but visible playback-follow and richer loop transport are not implemented here.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 29 - Transport, Playhead, Loop, and Tempo Map Integration

Summary:
- Extended the playback abstraction with transport-position methods:
  - get playhead position
  - set playhead position
  - return to start
  - enable/disable loop playback
- Tracktion playback now converts transport position through the Tracktion tempo sequence back into core PPQ ticks, so the UI playhead follows tempo-aware playback time.
- Project sync now also writes core time signature markers into Tracktion's tempo/time-signature sequence.
- Project sync preserves the requested playhead position after rebuilding the Tracktion edit.
- Added an optional transport Loop toggle that loops the prepared project playback range.
- Added a transport return-to-start button.
- Timeline ruler clicks now seek the engine transport through `AppServices`.
- Main UI now follows the engine playhead on a lightweight 24 Hz timer and pushes the playhead into the harmonic header, timeline, and piano roll.
- Timeline bar labels and clip-position text now use `TimeSignatureMap::fromTicks`.
- Timeline default clip and structure-region durations now use the active time signature's bar duration at the insertion point.
- Added a core test for bar/beat conversion across 4/4, 3/4, and 7/8 markers.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual verification with an actual VST3 instrument is still required for audible project playback and loop playback.
- The UI playhead follows Tracktion transport state by polling from the message thread at 24 Hz; this is deliberately lightweight but not yet a dedicated transport observer.
- Timeline horizontal spacing is still quarter-note based; bar labels and bar/beat text are meter-aware, but the visual grid is not yet a fully adaptive meter grid for every denominator.
- Project sync still rebuilds the Tracktion edit on the message/control path when playback is dirty; suitable for MVP, not yet optimized for large sessions.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 30 - Implement Diatonic Chord Stacking Hotkeys

Summary:
- Added undoable `StackDiatonicThirdCommand`.
- Added undoable `RemoveHighestChordToneCommand`.
- Chord stacking groups selected notes by matching start/duration and adds a diatonic third above the highest selected tone in each group.
- Repeated stacking keeps the original and generated notes selected, enabling root + third + fifth + seventh workflows.
- Generated notes preserve the source note start, duration, and velocity.
- Generated note spelling comes from the active scale instance at the note start time.
- Active harmonic context is resolved from the project musical structure at the note's project position.
- Custom project scales are included in the scale library used by the command.
- Cmd/Ctrl + Up in the piano roll stacks a diatonic third.
- Cmd/Ctrl + Down in the piano roll removes the highest selected chord tone.
- Added core tests for:
  - C in C Major -> C E
  - repeated C Major stacking -> C E G B
  - D in C Major -> D F
  - C Mixolydian seventh -> C E G Bb
  - stacking undo/redo
  - highest-tone removal and undo

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual verification of the piano-roll Cmd/Ctrl + Up/Down hotkeys should be done interactively; automated coverage verifies the core command behavior.
- The command intentionally does not do full chord recognition; it uses selected-note timing groups as Prompt 30's predictable chord construction model.
- If a selected source note is outside the active scale, it is skipped and no chord tone is generated for that note group.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 31 - Implement Chord Inversions and Single-Note Octave Movement

Summary:
- Added undoable `InvertChordCommand`.
- Shift + Up in the piano roll now runs `InvertChordCommand::Direction::upward`.
- Shift + Down in the piano roll now runs `InvertChordCommand::Direction::downward`.
- A single selected note moves by one octave while preserving timing, duration, velocity, and spelling.
- Multiple selected notes are grouped by matching start/duration and treated as vertical chord stacks.
- Upward inversion moves the lowest selected chord tone up one octave.
- Downward inversion moves the highest selected chord tone down one octave.
- Multiple selected chord stacks can be inverted together; unrelated singleton notes are ignored when more than one note is selected.
- Added core tests for:
  - C E G B upward inversion -> E G B C
  - downward inversion returning to root-position C E G B
  - single C4 moving to C5 then back to C4
  - undo/redo for chord inversion

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual verification of the piano-roll Shift + Up/Down hotkeys should be done interactively; automated coverage verifies the core command behavior.
- The inversion command intentionally does not perform chord recognition; it uses selected-note timing groups for predictable Prompt 31 behavior.
- Octave movement can fail at MIDI pitch boundaries because pitches must stay in the valid 0-127 MIDI range.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 32 - Implement Chord Recognition Core

Summary:
- Added core music-theory chord model files:
  - `Chord.h/.cpp`
  - `ChordQuality.h/.cpp`
  - `ChordRecognizer.h/.cpp`
  - `ChordNameFormatter.h/.cpp`
- `ChordRecognizer` recognizes chords from current MIDI pitches or pitch classes, independent of note generation history.
- Recognition normalizes inversions by trying each selected pitch class as a possible root.
- Ambiguous voicings prefer the lowest/current bass pitch, keeping simple sus and power-chord cases predictable.
- Added clean chord-name formatting without Roman numerals.
- Recognized qualities now include:
  - Major
  - Minor
  - Diminished
  - Augmented
  - Sus2
  - Sus4
  - Power chord
  - Maj7
  - Dominant 7
  - Minor 7
  - Half-diminished 7
  - Diminished 7
  - Minor-major 7
  - add9
- Added dedicated core tests for Prompt 32 examples and supported optional qualities.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- No UI was added for chord names in this prompt.
- No Roman numeral analysis was added.
- Recognition intentionally matches exact requested pitch-class sets and basic optional extensions, not broad jazz-symbol inference.
- Enharmonic root spelling currently uses formatter spelling preference rather than harmonic context.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 33 - Implement Chord Progression Lane Model and Globalize Command

Summary:
- Added `ChordProgressionLane` as the song-level owner of chord regions.
- Extended `ChordRegion` to store:
  - global song-level region start/end
  - chord root pitch class
  - chord quality
  - normalized chord tones
  - display chord name
- Kept `MusicalStructure::addChordRegion`, `removeChordRegion`, and `chordRegions` as wrappers over the chord progression lane.
- Added undoable `GlobalizeChordProgressionCommand`.
- The command analyzes selected notes from one MIDI clip and creates global chord regions in project time.
- MVP detection rule:
  - selected notes are sorted by clip-local start
  - notes starting within 15 ticks of the group's first note are treated as one vertical chord stack
  - each recognizable stack becomes one chord region
  - a region extends to the next detected chord start, or to that stack's selected-note end for the final chord
- Chord recognition uses the Prompt 32 recognizer, so inversions are normalized and names are chord symbols only.
- Extended project serialization to round-trip root, quality, chord tones, and chord name for chord regions.
- Added core tests for:
  - globalizing C E G to a C region
  - globalizing E G C to a normalized C region
  - globalizing C F G to a Csus4 region
  - multiple selected chord stacks creating multiple regions
  - chord-region serialization round trip
  - globalize undo/redo

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- No chord progression lane UI was added in this prompt.
- No borrowed chord suggestions were added.
- The globalize command starts with selected notes only; time-range analysis is intentionally deferred.
- The MVP grouping tolerance is fixed at 15 ticks and may need to become user/configurable later.
- Chord root spelling still comes from `ChordNameFormatter` spelling preference rather than active harmonic context.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- The macOS static-library build also emits existing no-symbol `ranlib` warnings for JUCE audio processor helper objects.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 34 - Add Chord Progression Lane UI

Summary:
- Added a Chord Progression lane to the timeline's musical structure area.
- Chord regions render as song-level regions with chord names only.
- Chord regions can be selected in the lane.
- Chord regions can be moved and resized with timeline drag gestures.
- Chord regions can be deleted with Delete/Backspace.
- Added command-backed chord-region editing:
  - `ReplaceChordRegionCommand`
  - `DeleteChordRegionCommand`
- Added a timeline `Globalize Chords` button with tooltip/action text:
  - `Globalize Chord Progression in Clip's Current Region`
- The globalize action uses the selected MIDI clip plus the selected note IDs from the open piano roll, then runs `GlobalizeChordProgressionCommand`.
- Added read-only selected-note accessors from `PianoRollComponent` through `DetailEditorComponent` to support the timeline action without moving note selection into project state.
- Manual chord-name editing is deferred and reports a message until chord-symbol parsing exists.
- Added core command tests for replacing and deleting chord regions with undo/redo.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual verification is still required for the full UI workflow: open a clip, select chord notes in the piano roll, click Globalize Chords, then move/resize/delete the generated chord regions.
- The Globalize action currently requires selected notes in the open piano roll; time-range globalizing is still deferred.
- Manual chord-name typing is deferred until a safe chord-symbol parser can update root, quality, tones, and display name together.
- No chord overlays were added.
- No borrowed chord suggestions were added.
- Chord root spelling still comes from `ChordNameFormatter` spelling preference rather than active harmonic context.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 35 - Add Chord-Tone and Non-Chord-Tone Piano Roll Overlay

Summary:
- Added core `HarmonicOverlay` classification for piano-roll harmonic overlay roles:
  - `none`
  - `root`
  - `chordTone`
  - `nonChordScaleTone`
  - `accidental`
- Overlay classification is based on the song-level Chord Progression lane and active key/scale context at the queried project tick.
- Classification rule:
  - outside chord regions -> no overlay
  - inside chord regions, non-scale pitch classes classify as accidentals first
  - scale tones then classify as root, chord tone, or non-chord scale tone
- Chord extensions are supported by stored `ChordRegion::chordTones`, so Cmaj7 highlights C E G B and future extended chord regions can highlight their stored tones.
- Piano roll now renders translucent spatial overlay bands over chord regions:
  - root lanes use a stronger gold highlight
  - chord tones use green highlight
  - non-chord scale tones use a very subtle neutral highlight
  - accidentals use the existing accidental-family purple precedence
- Chord region names are shown in the piano-roll ruler over their time spans.
- Overlay rendering is visual only and does not alter MIDI data or restrict note entry.
- Added pure core overlay tests for:
  - C major chord over C major scale
  - D minor region after C major region
  - root identification
  - non-chord scale tone classification
  - accidental precedence over chord tones
  - stored extension tones
  - no overlay outside chord regions

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual visual verification is still required for the piano-roll overlay colors and spatial readability.
- Overlay segmentation currently uses chord-region boundaries and classifies each segment at its start tick.
- No borrowed chord detection or compatible mode suggestions were added.
- No note-entry restrictions were added.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 36 - Borrowed Chord Detection and Compatible Mode Suggestions

Summary:
- Added core `BorrowedChordAnalysis` utilities:
  - `isChordDiatonicInContext`
  - `isBorrowedChordRegion`
  - `compatibleScaleSuggestionsFor`
- Borrowed detection checks whether every stored chord tone is inside the active key/scale context at the chord region start.
- Compatible suggestions search the project scale library, including custom scales supplied to the library by callers.
- MVP suggestion search keeps the current key center and finds scale/mode definitions that make the chord tones diatonic.
- Chromatic/all-12-note utility scales are skipped as practical suggestions.
- Chord Progression lane now color-codes borrowed/non-diatonic chords with a warmer borrowed-chord color.
- Right-clicking a borrowed chord region opens a `Find Compatible Modes` submenu.
- Selecting a compatible mode adds a song-level `ScaleModeRegion` over the chord's duration via `AddScaleModeRegionCommand`, so undo/redo works through the existing command stack.
- Right-clicking a diatonic chord reports that it is diatonic instead of treating it as an error.
- Added core tests for:
  - Bb chord in C Major is borrowed
  - Bb chord in C Mixolydian is diatonic
  - compatible mode search for Bb in C suggests Mixolydian
  - applying Mixolydian adds a scale/mode region
  - undo/redo restores and reapplies the suggestion region

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual UI verification is still required for borrowed chord coloring, right-click menu behavior, and applying a suggestion from the popup.
- Suggestion search is intentionally same-key-center MVP behavior only.
- Applying a suggestion currently adds a scale/mode region over the chord duration; it does not merge or reshape neighboring scale regions yet.
- Borrowed detection classifies using the chord region start tick; future prompts may need denser analysis if a chord region spans multiple harmonic contexts.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 37 - Project Rhythmic Settings and Tuplet Availability

Summary:
- Added project-level `ProjectRhythmSettings` for the active grid division and tuplet enablement flags.
- Extended the grid division registry with:
  - standard whole, half, quarter, eighth, sixteenth, thirty-second, and sixty-fourth divisions
  - configurable triplet divisions
  - optional quintuplet, septuplet, and nonuplet divisions
- Added `SetProjectRhythmSettingsCommand` so UI rhythm edits go through the command stack and support undo/redo.
- Serialized rhythm settings under `rhythmicSettings`, including active grid ID and tuplet flags.
- Added a piano-roll toolbar grid selector for the current grid / shortest note length.
- Added a piano-roll tuplet menu for enabling/disabling triplets, quintuplets, septuplets, and nonuplets.
- Piano-roll grid rendering, note entry duration, click snapping, note dragging, note resizing, and note duplication now use the active project grid.
- Added core tests for disabled tuplets, enabled quintuplets, rhythm-settings serialization, and rhythm-settings command undo/redo.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual UI verification is still required for changing grid values in the piano-roll toolbar, toggling tuplets, and confirming visible grid/snapping behavior by editing notes.
- Quantization and arpeggiator option integration is intentionally deferred to later prompts; Prompt 37 only prepares the shared grid registry/settings used by those future features.
- Custom arbitrary tuplet entry was not added; the implemented UI supports the requested named tuplet families.
- Project schema remains at version 1 and currently requires `rhythmicSettings` in current serialized documents.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 38 - Implement Arpeggiation Hotkeys

Summary:
- Added core arpeggiation support:
  - `ArpeggioPattern`
  - pattern cycling helpers
  - enabled-grid arpeggio subdivision ordering
  - shorter/longer subdivision stepping
- Added `ArpeggiateSelectionCommand` for destructive, undoable MIDI arpeggiation.
- The command replaces selected notes with real MIDI notes, so MIDI export sees the arpeggio as ordinary clip note data.
- The command preserves pitch spelling and velocity from source chord tones where possible.
- The command supports deterministic patterns:
  - Up
  - Down
  - Up-Down
  - Down-Up
  - Outside-In
  - Inside-Out
- Random pattern was deferred to keep tests deterministic.
- Arpeggio subdivisions are resolved from enabled project grid divisions; disabled tuplets are not available.
- Piano roll hotkeys now trigger command-backed arpeggiation:
  - Option/Alt + Right: shorter subdivision
  - Option/Alt + Left: longer subdivision
  - Option/Alt + Down: next pattern
  - Option/Alt + Up: previous pattern
- Repeated hotkey use can re-arpeggiate the selected arpeggio output by reconstructing source chord tones from the selected note span.
- Added tests for:
  - C E G B arpeggiating upward
  - Down pattern
  - shorter/longer subdivision stepping
  - disabled quintuplet absence
  - enabled quintuplet availability
  - undo/redo

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual UI verification is still required for the Option/Alt arpeggiation hotkeys in the piano roll.
- There is not yet a visible piano-roll status control for current arpeggio pattern/subdivision; changes are logged and reflected in note edits.
- Random arpeggio pattern is intentionally deferred.
- The app bundle built by CMake is at `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app`.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 39 - MIDI Input Recording

Summary:
- Added an app-layer `MidiInputRecordingService` backed by JUCE MIDI input devices.
- Added plain app-facing MIDI input types so UI code does not include JUCE audio-device headers through `AppServices`.
- MIDI input callback behavior is intentionally small:
  - accepts note-on/note-off messages only
  - copies compact events into a bounded preallocated ring buffer
  - does not mutate project state or UI state
- `MainComponent` drains queued MIDI input events on the message thread.
- Added command-backed recording transactions in `AppServices`:
  - note-on captures armed track, target clip, local start, velocity, and exact MIDI pitch
  - note-off adds the recorded note through `AddNoteCommand`
  - clips are extended through `ResizeClipCommand` if a note runs past the current clip end
  - newly needed recording clips are created through `AddClipCommand`
- Recording rule:
  - recording requires a record-armed track
  - if the currently opened clip is on the armed track, recording goes into that clip
  - otherwise the first recorded note creates a new four-bar `MIDI Recording` clip on the armed track at the current recording tick
- Recorded pitches are exact by default; no Scale Lock or input quantization was added.
- Incoming note spelling uses the current exact pitch class spelling, so F# records as F# and appears as a used accidental in C Major.
- Added transport controls:
  - MIDI input device combo box
  - MIDI `Rec` toggle
- Added per-track ARM chips in the track list.
- The app target now links `juce::juce_audio_devices` for MIDI input access.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual hardware verification is still required with a real MIDI keyboard/device.
- There is not yet a dedicated MIDI recording settings panel; Prompt 39 uses compact transport and track-list controls.
- Recording creates normal project notes immediately on note-off, so undo works per command but not yet as one grouped recording transaction.
- Input quantization and Scale Lock are intentionally deferred to Prompt 40.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 40 - Input Quantization and Scale Lock

Summary:
- Added core `RecordingInputTransform` helpers for recording-time MIDI transforms:
  - input quantization grid availability
  - note-start quantization to enabled project grid divisions
  - Scale Lock pitch mapping
  - recorded pitch spelling after Scale Lock
- Input quantization uses the project's current grid / shortest-note setting from `ProjectRhythmSettings`.
- Tuplet quantization availability follows project tuplet settings; disabled quintuplet grids are unavailable until enabled.
- Added Scale Lock modes:
  - Off
  - Nearest
  - Round Up
  - Round Down
- Scale Lock is off by default.
- Nearest mapping resolves exact ties upward for deterministic behavior.
- Recording now applies:
  - optional input quantization to note starts on note-on
  - optional Scale Lock based on active key/scale context at the performed note-on time
  - stored MIDI pitch is the resulting mapped pitch
- Added compact transport controls:
  - `Q` input quantize toggle
  - Scale Lock mode combo
- Added core tests for:
  - sixteenth-note input quantization
  - quintuplet quantization unavailable until enabled
  - Scale Lock off preserving F# in C Major
  - nearest mapping F# in C Major to G
  - round up/down mapping F# in C Major to G/F

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and stopped cleanly

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke test succeeded without crashing.

Known issues:
- Manual hardware verification is still required with a real MIDI keyboard/device.
- Input quantization currently quantizes note starts only; note ends/durations remain performed durations.
- Scale Lock does not store original performed pitch metadata yet.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## Prompt 41 - Project Save/Load and Plugin State Persistence

Summary:
- Added `.tseq` project UI actions:
  - New Project
  - Open Project
  - Save Project
  - Save Project As
- Added a compact `Project` menu button to the transport bar.
- Added app service project lifecycle methods for new/open/save/save-as.
- Project open/save is orchestrated through `AppServices`; UI does not directly mutate project state.
- Added `TrackInstrumentReference::pluginStateFile` and serialized it in `project.json` alongside the existing track plugin reference.
- Kept binary plugin state out of `project.json`; plugin states are written under package-local `plugin-states/`.
- `ProjectPackage::save` now creates placeholder plugin state files for serialized state references, preserving safe round trips when the plugin is unavailable.
- Extended the playback abstraction with:
  - `setProjectPluginStateDirectory`
  - `captureTrackPluginStates`
- Implemented Tracktion-backed plugin state capture/restore behind `TracktionPlaybackEngine`.
- Missing plugins are handled safely:
  - project/track musical data remains loaded
  - plugin references and state-file references remain serialized
  - warnings are emitted through diagnostics/status logging
- Save/load stops playback and runs from the app/UI side, not from the audio callback.
- Added core serialization/package tests for:
  - plugin state file reference round trip
  - missing plugin reference preservation
  - plugin-state placeholder file creation in `.tseq` packages

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke reached initialization and loaded the existing plugin registry/audio settings.

Known issues:
- Manual VST3 round-trip verification is still required: assign an instrument, edit state, save, reopen, and confirm restore.
- Recent projects were not added; this would require app settings/schema work and was not treated as straightforward for this prompt.
- App launch smoke still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during startup plugin-format setup, but the app continued initialization.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.
- Persistent docs requested by Prompt 01 remain absent; `docs/agent/*` continues to be the active source of truth.

## UI/UX Bug Pass - Workspace Header, Shortcuts, Timeline, Piano Roll, Arpeggios

Summary:
- Removed the large harmonic/project header from the main workspace layout.
- The timeline and structure lanes now have the reclaimed vertical space.
- Added main-surface global shortcut handling for:
  - Command/Ctrl+Z undo
  - Command/Ctrl+Shift+Z redo
  - Command/Ctrl+Y redo
  - Spacebar transport start/stop
- Saved/opened project packages now update the window title from the `.tseq` package name.
- Added continuous timeline playhead dragging from the ruler.
- Structure lane regions are no longer clipped by the left-side lane labels, allowing key/scale/chord regions to reach beat 1.
- Made the structure lanes taller for more reliable region interaction.
- Added timeline hover cursors for playhead/ruler movement and resize-edge affordances.
- Changed piano roll note creation/deletion behavior:
  - single-click empty space clears/keeps selection only
  - double-click empty grid creates a note through `AddNoteCommand`
  - double-click an existing note deletes it through `DeleteNoteCommand`
  - Command/Ctrl+A selects all notes in the open piano roll
  - note-edge hover cursors indicate start/end resizing
- Fixed `ArpeggiateSelectionCommand` so multiple selected chord spans are arpeggiated independently instead of collapsing into one arpeggio over the full selected range.
- Added a regression test for independent C/F chord-span arpeggiation.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke reached initialization and restored the existing audio settings.

Known issues:
- Marquee selection is not implemented yet for timeline clips, structure regions, or piano-roll notes.
- Command/Ctrl+A is implemented for piano-roll notes only in this pass.
- Command/Ctrl+scroll horizontal zoom and Option/Alt+scroll piano-roll vertical zoom are not implemented yet.
- Piano roll panel resizing by dragging its top edge is not implemented yet.
- Timeline clip start resizing still needs a command-level grouped mutation before it should be enabled.
- Default key/scale visual regions are still generated display regions until an explicit region exists; deeper default-region drag behavior remains to do.
- The requested custom open/closed bracket cursors are approximated with native left/right edge resize cursors for now.
- App launch smoke still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`, but the app continued initialization.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.

## UI/UX Bug Pass - Marquee, Zoom, Piano Roll Resize, Select All, Default Regions

Summary:
- Added marquee selection in the piano roll:
  - click-drag empty grid draws a marquee
  - notes intersecting the marquee become selected
  - note edits still use existing note commands
- Added marquee selection in the timeline:
  - click-drag empty track space selects intersecting MIDI clips
  - click-drag empty structure-lane space selects intersecting key/scale/chord regions
  - selected clips/regions are visually highlighted
- Added context-aware Command/Ctrl+A:
  - piano roll focus selects all notes in the open clip
  - timeline clip focus selects all MIDI clips
  - structure-lane focus selects all structure regions, including displayed default key/scale regions
- Added scroll zoom:
  - Command/Ctrl+scroll in the piano roll changes horizontal grid scale
  - Option/Alt+scroll in the piano roll changes vertical note-lane height
  - Command/Ctrl+scroll in the timeline changes horizontal scale
  - Command/Ctrl+scroll over structure lanes only zooms when over the tempo lane
- Added draggable piano-roll top splitter in the main workspace.
- Default key and scale display regions are now selectable/drag targets when no explicit regions exist.
- Dragging a default key or scale display region creates an explicit command-backed region with the default key/scale value.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke reached initialization and restored the existing audio settings.

Known issues:
- Multi-selection is currently visual/contextual selection; most destructive or transform commands still operate on the primary selected item unless a command already supports multiple IDs.
- Timeline clip start resizing remains a cursor affordance only; enabling it safely still needs a command-level move+resize mutation.
- Scroll zoom does not preserve the exact visual anchor point under the mouse yet.
- Piano-roll top resizing is handled by the main workspace splitter gap, not by dragging directly on the piano-roll component itself.
- The requested custom bracket cursors are still approximated with native resize cursors.
- App launch smoke still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`, but the app continued initialization.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.

## UI/UX Bug Pass - Copy/Paste, Region Delete, Piano Roll Fixes

Summary:
- Added undoable core commands:
  - `AddChordRegionCommand`
  - `DeleteKeyCenterRegionCommand`
  - `DeleteScaleModeRegionCommand`
- Added timeline copy/paste:
  - Command/Ctrl+C copies selected MIDI clips
  - Command/Ctrl+V pastes copied clips to the playhead, or after the copied range when the playhead is at start
  - pasted clips receive fresh clip IDs and preserve notes, loop state, and harmonic metadata
- Added musical-structure copy/paste:
  - Command/Ctrl+C copies selected key/scale/chord regions
  - Command/Ctrl+V pastes copied regions to the playhead, or after the copied range
  - chord regions paste through `AddChordRegionCommand`
  - key/scale regions paste through their existing add commands
- Added deletion for key center and scale/mode regions, alongside existing chord-region delete support.
- Added piano-roll note copy/paste:
  - Command/Ctrl+C copies selected notes
  - Command/Ctrl+V pastes into the open clip with fresh note IDs
  - paste uses the local playhead when it is inside the clip, otherwise it pastes after the copied range
- Fixed diatonic third stacking in non-C key centers so wrapped scale degrees do not jump an extra octave.
- Added a regression test for stacking E to G in G major.
- Added piano-roll note-end resizing:
  - Shift+Left moves selected note ends to the previous grid line
  - Shift+Right moves selected note ends to the next grid line
- Added single-note melody octave shifting:
  - Shift+Up transposes selected non-overlapping melody notes up an octave
  - Shift+Down transposes selected non-overlapping melody notes down an octave
  - chord-stack selections still use the existing inversion behavior
- Reduced scroll zoom sensitivity in the piano roll.
- Opening a clip frames around C3 when it has no notes.
- Piano-roll edits now try to keep notes in view after note mutations.
- Reworked piano-roll arpeggio cycling so the first arpeggiation captures the original selected chord stack and subsequent Alt/Option arrow changes reuse that source.

Commands run:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- Full build succeeded.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke reached initialization and restored the existing audio settings.

Known issues:
- Piano-roll arpeggio cycling now preserves source chord tones during a continuous arpeggio-edit session, but the replacement is implemented as a sequence of delete/add note commands rather than one grouped undo command.
- Timeline clip paste avoids existing overlaps one clip at a time, but does not yet show a preview or conflict UI.
- Region paste does not yet show overlap/conflict diagnostics for overlapping musical-structure regions.
- App launch smoke still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`, but the app continued initialization.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.

## Prompt 42 - Audio Thread Safety and Performance Audit

Summary:
- Added `docs/AUDIO_THREAD_AUDIT.md` documenting:
  - realtime/audio callback ownership
  - the current prepared playback boundary
  - MIDI input callback behavior
  - allowed realtime operations
  - remaining risks and planned follow-ups
- Confirmed TheorySequencer does not currently implement a custom audio render callback; Tracktion Engine owns render processing.
- Documented `PlaybackEngine` as a control-thread API whose implementations prepare playback data before backend rendering.
- Added debug message-thread assertions around Tracktion playback control methods.
- Marked `TracktionPlaybackEngine::syncProject()` as the non-realtime preparation boundary.
- Documented that project MIDI is materialized into Tracktion MIDI clips before playback instead of traversing the core project model from the audio callback.
- Tightened the JUCE MIDI input callback path:
  - documented it as allocation-free and lock-free
  - kept theory analysis, sorting, command execution, logging, and UI work on the message-side drain path
  - changed the dropped-event counter to relaxed atomic load/increment

Commands run:
- `cmake --build build --target tsq_app`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- App target built successfully.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke reached initialization, restored the existing audio output, and did not trip the new message-thread assertions.

Known issues:
- The persistent `docs/AUDIO_THREAD_RULES.md` file requested by the original prompt sequence still does not exist; Prompt 42 used `docs/agent/02_ARCHITECTURE_GUARDRAILS.txt` as the active audio-thread rule source.
- Manual VST3 playback verification with an actual instrument was not performed in this pass.
- Third-party plugin realtime behavior remains outside this codebase's direct control.
- Tracktion Engine owns the render callback; this audit verifies TheorySequencer's adapter boundary, not Tracktion internals.
- `syncProject()` remains synchronous on the message path. This is audio-thread safe for MVP but may need worker/prepared-snapshot flow for large sessions or live editing.
- MIDI input queue overflow is counted but not yet surfaced in the UI.
- App launch smoke still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`, but the app continued initialization.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.

## Prompt 43 - Error Handling, Crash Safety, and Diagnostics Pass

Summary:
- Added `core::diagnostics::Result` as the shared success/failure result type and moved `CommandResult` onto that alias.
- Added non-throwing MIDI export through `MidiExporter::tryExportClipToFile()`, with explicit open/write failure messages that include the target path.
- Added an app diagnostics overlay available from the status bar `Log` button, showing formatted diagnostic log entries with Refresh and Close controls.
- Added app-level user message plumbing through `AppServices::reportWarning()`, `reportError()`, `lastUserMessage()`, and `clearUserMessage()`.
- Surfaced recent warnings/errors in the status bar so command, plugin, audio, project, recording, and MIDI-input failures are visible outside the console.
- Reworked project save/load failure UI to display the specific app service error when available.
- Reworked plugin load/assignment, plugin registry load, audio output/settings, playback sync/start, MIDI recording, plugin-state writing, and timeline command failures to report useful user-visible diagnostics.
- Loading a project now warns when a track references a plugin that is not present in the current registry.
- Added focused regressions for:
  - shared diagnostic result semantics
  - invalid null command execution
  - invalid project package JSON load errors
  - non-throwing MIDI export file failure
- Confirmed the existing missing plugin reference serialization regression remains in place.

Commands run:
- `cmake --build build --target tsq_app`
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- App target built successfully.
- Rebuilt `tsq_core_tests` successfully with the new regression cases.
- `tsq_core_tests` passed: 1 CTest executable, 0 failures.
- App launch smoke reached initialization, loaded the plugin registry, restored the existing audio output, and stayed alive until stopped.

Known issues:
- Diagnostics overlay open/close was compile- and launch-smoked, but not screenshot-verified or clicked by UI automation in this pass.
- Missing-plugin user warnings are covered in the app load path and by an existing serialization preservation test; the full warning path was not automated because it depends on app services, plugin registry state, and Tracktion initialization.
- Failed third-party plugin loading remains dependent on Tracktion/JUCE plugin hosting behavior, but app-level failure paths now report the Tracktion status message instead of silently failing.
- No MIDI export UI exists yet; the export failure path was tested through the new non-throwing core API.
- App launch smoke still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`, but the app continued initialization.
- The app build still emits the existing harmless linker warning about duplicate `tsq_core` static-library linkage when relinking `tsq_app`.

## Prompt 44 - Packaging and Cross-Platform Build Preparation

Summary:
- Added `docs/BUILDING.md` with macOS, Windows, and Linux build instructions, preset usage, platform generator notes, and headless test commands.
- Added `docs/DEPENDENCIES.md` documenting expected local dependency paths, pinned dependency versions, VST3 requirements, path override variables, and commercial licensing reminders.
- Added `docs/MANUAL_QA_CHECKLIST.md` covering launch, plugin scan, VST3 instrument load, clip creation, note drawing, playback, save/load, and MIDI export.
- Added `CMakePresets.json` with:
  - `debug` app configure/build preset
  - `release` app configure/build preset
  - `tests` headless core-test configure/build/test preset
- Added `TSQ_BUILD_APP` so the desktop app, UI, engine, JUCE, and Tracktion dependencies can be excluded from pure core-test builds.
- Updated dependency validation so enabled options determine which local dependency directories are required.
- Labeled `tsq_core_tests` as `unit`, `pure`, and `headless`.
- Added `.github/workflows/ci.yml` for macOS/Linux headless core tests. The workflow fetches the pinned Catch2 tag and uses the `tests` preset, so it does not require JUCE, Tracktion Engine, installed plugins, audio hardware, or MIDI hardware.
- Updated `README.md` to point to the new build, dependency, and QA docs.

Commands run:
- `cmake --list-presets=all`
- `cmake --preset tests`
- `cmake --build --preset tests`
- `ctest --preset tests`
- `cmake --preset debug`
- `cmake --build --preset debug`
- `cmake --preset release`
- `out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- Preset discovery succeeded for configure, build, and test presets.
- `tests` preset configured without JUCE or Tracktion Engine; only Catch2 appeared under `out/build/tests/_deps`.
- `tests` preset built `tsq_core_tests` successfully.
- `ctest --preset tests` passed: 1 CTest executable, 0 failures, with `headless`, `pure`, and `unit` label summaries.
- `debug` preset configured and built `tsq_app` successfully.
- `release` preset configured successfully.
- App launch smoke from the debug preset build reached initialization, loaded the plugin registry, restored the existing audio output, and stayed alive until stopped.

Known issues:
- The GitHub Actions workflow was added but not run on GitHub in this local pass.
- Windows and Linux build instructions were documented but not locally verified on those platforms.
- The `release` preset was configured but not fully built in this pass.
- Installer packaging, notarization, and signing were intentionally not added.
- App preset build still emits existing third-party Tracktion/JUCE warnings, macOS no-symbol `ranlib` notices, and the duplicate `tsq_core` linker warning.
- App launch smoke still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`, but the app continued initialization.

## Prompt 45 - MVP Acceptance Test and Bug Fix Pass

Summary:
- Read the final MVP prompt and active docs. The persistent `docs/AI_CONTEXT.md`, `docs/PRODUCT_SPEC.md`, `docs/ARCHITECTURE.md`, and `docs/ROADMAP.md` files are still absent, so this pass used `docs/agent/*` plus `docs/IMPLEMENTATION_LOG.md` as the source of truth.
- Mapped the 20-step MVP scenario against the current app implementation.
- Fixed the primary MVP gap from the previous prompt by adding a UI MIDI export path:
  - Project menu now includes `Export Open Clip as MIDI...`
  - export uses the clip currently open in the piano roll
  - export defaults to the project package `exports/` folder when the project has been saved
  - export uses the project tempo and time signature at the clip start
  - failures are surfaced through app diagnostics and a warning alert
- Added open-clip ID accessors through `DetailEditorComponent` and `PianoRollComponent` so main UI actions can operate on the currently edited clip without reaching into component internals.
- Added a pure MVP-shaped regression covering:
  - project package save/load
  - plugin reference preservation
  - clip/note preservation
  - F# used-accidental lane visibility in C Major
  - MIDI export into the project package `exports/` folder
- Added `docs/KNOWN_ISSUES.md` with MVP verification gaps, runtime issues, build/packaging issues, and missing persistent-doc issues.

MVP acceptance status:
- Launch app: launch-smoked successfully.
- Scan VST3 plugins: registry loaded from the existing cache during launch; full scan was not re-run manually in this pass.
- Create project, tracks, clip, draw notes, chromatic reveal, chord stacking, inversion, save/load: implementation paths exist and are partially covered by core regressions, but the full native UI sequence was not automated in this environment.
- Export clip as MIDI: UI path added; core export and package export are covered by tests.
- Play project through VST3 instrument: not re-verified manually in this pass; documented in `docs/KNOWN_ISSUES.md`.
- Data/plugin/note/scale-mode/accidental preservation: covered by existing serialization tests plus the new MVP package/export regression.

Commands run:
- `cmake --build build --target tsq_app`
- `cmake --build build --target tsq_core_tests`
- `ctest --test-dir build --output-on-failure`
- `cmake --build --preset tests`
- `ctest --preset tests`
- `build/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- App target built successfully.
- Rebuilt `tsq_core_tests` successfully with the new MVP regression.
- `ctest --test-dir build --output-on-failure` passed: 1 CTest executable, 0 failures.
- Headless `tests` preset rebuilt the changed test and passed: 1 CTest executable, 0 failures, with `headless`, `pure`, and `unit` label summaries.
- App launch smoke reached initialization, loaded the plugin registry, restored the existing audio output, and stayed alive until stopped.

Known issues:
- Full interactive MVP QA should still be run by a human with macOS assistive access or directly in the app; this environment did not complete native UI automation for the full 20-step scenario.
- Manual VST3 instrument assignment/playback was not re-run end-to-end in this pass.
- The new MIDI export file chooser should be clicked manually in the next interactive QA pass, even though the code path builds and the core export path is tested.
- App launch smoke still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`, but the app continued initialization.
- The app build still emits the existing duplicate `tsq_core` static-library linker warning when relinking `tsq_app`.

## Prompt 46 - Per-Note Harmonic Interpretation for Selected Scale-Degree Transpose

Summary:
- Added per-note harmonic interpretation metadata so a note can remember the harmonic lens it was authored or copied from:
  - source harmonic context
  - scale degree index within that source scale
  - accidental alteration relative to that scale degree
- Added note interpretation helpers for:
  - inferring a note's source degree/alteration from a harmonic context
  - mapping that interpretation into a target harmonic context
  - spelling the remapped pitch
  - retargeting the stored metadata after a successful scale-degree transpose
- Serialized `MidiNote::harmonicInterpretation` with backward-compatible loading for older project files that do not contain the field.
- Added `ScaleDegreeTransposeSelectedNotesCommand`.
  - When selected notes are scale-degree transposed, each selected note uses its saved source interpretation and the song harmonic context at that note's current clip position.
  - Unselected notes in the same MIDI clip are left unchanged.
  - The existing full-clip scale-degree transpose command remains available when no notes are selected.
- Updated note creation and editing flows:
  - `AddNoteCommand` infers interpretation from the song harmonic context at the note position when the note does not already carry metadata.
  - pitch-changing `MoveNoteCommand` recomputes interpretation; horizontal-only movement preserves it.
  - undo restores the previous note interpretation.
  - paste, duplicate, and arpeggiation preserve source-note interpretation for derived notes.
  - stacked diatonic thirds store a fresh interpretation in the active harmonic context where they are generated.
- Updated the piano-roll toolbar so scale-degree transpose operates on the selected notes when there is an active selection, and falls back to the existing clip-wide behavior otherwise.
- Added regressions for:
  - a copied C Major motif selected in a B Whole Tone region transposing only the selected notes into B Whole Tone
  - per-note harmonic interpretation serialization round-trip

Commands run:
- `cmake --build --preset tests`
- `ctest --preset tests`
- `cmake --build --preset debug`
- `out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- Headless core tests built successfully.
- `ctest --preset tests` passed: 1 CTest executable, 0 failures.
- Debug app target built successfully.
- App launch smoke reached initialization, loaded settings and the plugin registry, and stayed alive until stopped.

Known issues:
- This pass verified startup and core regressions, but did not manually exercise the full piano-roll UI workflow in the running app.
- The debug app build still emits the existing macOS no-symbol `ranlib` notices for JUCE/Tracktion objects and the duplicate `tsq_core` linker warning.

## Prompt 47 - Preserve Live VST State Across Playback Sync

Summary:
- Fixed a VST state reset path in `TracktionPlaybackEngine::syncProject`.
- Before replacing the Tracktion edit during a project playback sync, the engine now captures the live in-memory state for each assigned project instrument.
- When the new edit is built, the engine restores that live per-track state onto the new plugin instance before falling back to saved package plugin-state files.
- Playback start also flushes live project plugin state before starting the transport, covering the no-rebuild path.
- This prevents unsaved synth/editor tweaks from being overwritten by stale/default plugin state when return-to-start or playback start forces a project sync.
- Reused the same live-state capture path for project-save state collection so save and sync read plugin state consistently.
- Follow-up: return-to-zero and ordinary playhead movement no longer force playback project sync. Project mutations such as clip creation or harmonic-region edits still mark playback dirty, but the rebuild is deferred until playback actually starts.
- Follow-up: live VST state capture now reads directly from the active `AudioPluginInstance`, then writes the captured chunk into Tracktion's plugin `state` tree as well as the inherited `elementState`, so delayed/asynchronous VST initialization can restore the captured sound.

Commands run:
- `cmake --build --preset debug`
- `cmake --build --preset tests`
- `ctest --preset tests`
- `out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer` launched briefly and was stopped

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.
- App launch smoke reached initialization, loaded settings and the plugin registry, and stayed alive until stopped.

Known issues:
- This pass verified the engine build and startup smoke, but did not manually load a synth, tweak parameters, return to beat one, and play through the UI.
- The debug app build still emits the existing macOS no-symbol `ranlib` notices for JUCE/Tracktion objects and the duplicate `tsq_core` linker warning.

## Prompt 48 - Preserve VST Instances During MIDI/Harmonic Playback Sync

Summary:
- Continued the VST reset investigation after live state restore alone did not resolve the issue.
- Removed app-level uses of Tracktion transport stops with `clearDevices=true` for ordinary shutdown, project sync, stop, test instrument load, test phrase play, and test phrase stop paths.
  - Normal stops now use `stop(false, false)` so Tracktion does not fully clear/reinitialize the playback graph as part of routine transport movement.
- Added an in-place project playback sync path for dirty project changes where the assigned track instruments have not changed.
  - MIDI clips are cleared and rebuilt on the existing Tracktion audio tracks.
  - Tempo/time signature state is refreshed.
  - Existing VST plugin instances are kept alive, including any open editor and unsaved parameter edits.
- Full edit rebuilds are still used when:
  - a new project/package path is loaded
  - the project track/instrument topology changes
  - an instrument assignment changes
- Live VST state capture now records the plugin identity with each state chunk.
  - A captured state chunk is restored only onto the same plugin identity, preventing one plugin's state from being applied to another plugin assigned to the same track.
  - New project loads intentionally skip old live state so saved package state is used instead.

Commands run:
- `cmake --build --preset debug`
- `cmake --build --preset tests`
- `ctest --preset tests`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.

Known issues:
- This pass was verified by build and automated tests only. The user will manually re-test the VST editor scenario in the app.
- The debug app build still emits the existing macOS no-symbol `ranlib` notices for JUCE/Tracktion objects and the duplicate `tsq_core` linker warning.

## Prompt 49 - Preserve VST State Around Transport Seeks

Summary:
- Continued the persistent VST reset investigation for the path:
  - assign a VST instrument
  - edit the synth parameters
  - create a MIDI clip and notes
  - return to zero
- Added direct live-state preservation around transport/playback lifecycle operations:
  - playback start captures live project instrument state before `transport.play()` and reapplies it after
  - playhead moves, including return-to-zero, capture live project instrument state before `transport.setPosition()` and reapply it after
  - in-place playback sync captures live project instrument state before stopping/rebuilding MIDI clips and reapplies it after
  - final project sync positioning also reapplies the restored state after setting the transport position
- Added sync-mode diagnostics:
  - in-place MIDI/harmonic sync reports `Project synced in place`
  - full edit rebuild reports `Project synced with full edit rebuild`
  - `AppServices` now includes that playback-engine message in the diagnostics log entry for project playback sync

Commands run:
- `cmake --build --preset debug`
- `cmake --build --preset tests`
- `ctest --preset tests`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.

Known issues:
- This pass still depends on manual VST editor verification because the automated test suite does not host an interactive third-party VST editor.
- If the reset persists, the new diagnostics log should indicate whether the app is unexpectedly doing a full edit rebuild during the repro path.

## Prompt 50 - Restore VST State After First MIDI Clip Graph Refresh

Summary:
- Refined the VST reset fix after the reset was narrowed to the moment a MIDI clip first exists on the assigned-instrument track.
- Confirmed Tracktion schedules `Edit::restartPlayback()` when MIDI clips are inserted/removed through the edit ValueTree watcher.
  - The playback graph refresh happens later on Tracktion's timer.
  - Previous state restoration could happen before that delayed graph refresh, allowing the refresh to overwrite the edited VST state afterward.
- Wrapped playback edit materialization in `TransportControl::ReallocationInhibitor` while rebuilding Tracktion MIDI clips.
- After clip creation/removal, the engine now explicitly calls `Edit::dispatchPendingUpdatesSynchronously()` before restoring live VST state.
  - This applies to both full playback edit rebuilds and in-place MIDI/harmonic syncs.
  - The edited synth state is restored after Tracktion has performed the graph refresh caused by the first MIDI clip, not before it.

Commands run:
- `cmake --build --preset debug`
- `cmake --build --preset tests`
- `ctest --preset tests`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.

Known issues:
- Manual VST editor verification is still needed because the automated tests do not host an interactive third-party VST editor.

## Ableton Mixer Prompt 04 - Tracktion Playback Graph Device Chains

Summary:
- Upgraded project playback sync from a single assigned-instrument path to a Tracktion-backed device-chain graph:
  - MIDI tracks host their instrument slot plus following VST3 audio-effect slots.
  - Audio and return tracks can host VST3 audio-effect slots.
  - Return tracks receive Tracktion aux-return plugins, and ordinary tracks materialize sends as aux-send plugins.
  - Master track volume/pan and master device-chain effects are applied to the Tracktion master plugin list.
- Added control-path application for track name, volume, pan, mute, solo, routing fallback warnings, send level/mute, device bypass, and audio clip materialization.
- Generalized live hosted-plugin state capture/restore from track-only instrument state to per-track/per-device-slot state keys, preserving the existing VST reset guard while preventing instrument/effect states from colliding.
- Updated project save preparation so legacy instrument state remains backward-compatible and device-chain slots get separate package state files.
- Conservatively forces a full Tracktion graph rebuild for sends, return tracks, non-default track routing, MIDI routing selections, and device bypass changes.
- Updated `docs/AUDIO_THREAD_AUDIT.md` with the new preparation boundaries and Prompt 05 metering/smoothing follow-ups.

Commands run:
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "AppServices saves separate plugin state placeholders for device-chain slots"`
- `cmake --build --preset tests --target tsq_core_tests && ctest --preset tests --output-on-failure`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "Synthesizer plugin parameters survive MIDI clip and note creation"`

Result:
- Debug app target builds successfully.
- Engine integration test target builds successfully.
- New package/plugin-state regression passes: 13 assertions in 1 test case.
- Core CTest suite passes: 1 executable, 0 failures.
- Plugin registry suite passes: 32 assertions in 4 test cases.
- Synthesizer VST state regression passes: 376 assertions in 1 test case.

Known issues:
- The integration test binary still prints a non-fatal JUCE debug assertion during Tracktion initialization on this machine.
- Manual audio smoke is still required with a real instrument plus real audio effect to confirm audible effect processing, mute/solo behavior, returns, and master chain on the user's audio device.

## Ableton Mixer Prompt 05 - Realtime Metering And Mixer Control Quality

Summary:
- Added JUCE/Tracktion-free mixer-control helpers:
  - dB/gain conversion and clamping with `-inf` handling
  - user-facing dB text parse/format helpers
  - linear and constant-power pan-law helpers
  - normalized send-level to dB conversion
  - small linear control-ramp helper for click-free control transitions
- Added core `MeterBallistics` for UI meter attack/release/decay and explicit stopped-transport reset behavior.
- Extended the playback abstraction with `PlaybackEngine::getMeterSnapshot()`.
- Added `MeterSnapshot`, `MeterSourceSnapshot`, and `MeterChannelSnapshot` engine DTOs so UI code can poll bounded app-owned meter state without Tracktion types.
- Wired Tracktion level-meter clients for every project track/return plus a guaranteed master meter:
  - Meter clients are attached after sync and detached before edit rebuild/shutdown.
  - Snapshots are sourced from Tracktion's actual `LevelMeterPlugin` graph taps.
  - Stopped transport returns floor/zero values and clears clients so meters do not remain stuck.
- Updated `docs/AUDIO_THREAD_AUDIT.md` with the meter snapshot boundary and remaining RMS/bypass-smoothing notes.

Commands run:
- `cmake --build --preset tests --target tsq_core_tests`
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `ctest --preset tests --output-on-failure`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "Synthesizer plugin parameters survive MIDI clip and note creation"`

Result:
- Core test target builds successfully.
- Debug app target builds successfully.
- Engine integration target builds successfully.
- New real-audio meter smoke test passes: 8 assertions in 1 test case.
- Core CTest suite passes: 1 executable, 0 failures.
- Plugin registry suite passes: 32 assertions in 4 test cases.
- Package/plugin-state regression passes: 13 assertions in 1 test case.
- Synthesizer VST state regression passes after the metering changes: 372 assertions in 1 test case.

Known issues:
- RMS fields exist in the snapshot DTOs but are marked unavailable until a dedicated simultaneous peak/RMS tap is added.
- The integration test binary still prints a non-fatal JUCE debug assertion during Tracktion initialization on this machine.
- Manual audio smoke is still required with a real mixer UI once Prompt 06+ surfaces visible meters.

## Ableton Mixer Prompt 06 - Right-Docked Browser Panel

Summary:
- Replaced the right-side scale-only area in `MainComponent` with a tabbed `BrowserPanelComponent`.
- Embedded the existing `ScalePaletteComponent` inside the Browser Panel's Scales tab so built-in scales, custom scales, search, and scale drag payloads continue to use the existing implementation.
- Added a Plugins tab that:
  - Lists cached VST3 plugin metadata from the existing registry.
  - Supports search plus All/Instruments/Audio Effects/Project Files filters.
  - Reuses the existing plugin scan service for Scan/Reload/status/search-path feedback.
  - Safely enumerates VST3 bundles, audio files, and MIDI files from the currently open `.tseq` project package, capped to avoid unbounded UI work.
- Added typed Browser Panel drag payload helpers for plugins and project files while preserving the existing `tsq-scale:` string payload for scale drops.
- Upgraded `TrackListComponent` into a plugin drop target:
  - Dragging an instrument plugin onto a track header assigns it to the track.
  - Dragging an audio-effect plugin onto a track header appends it to that track's device chain.
  - Drop targets validate the plugin against the registry before mutating the project.
- Kept the existing overlay Plugin Browser available for its scan, assignment, test phrase, and plugin-editor workflows.

Commands run:
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `ctest --preset tests --output-on-failure`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- App launch smoke with screenshot captured at `/tmp/theorysequencer-prompt06-browser-panel-app.png`

Result:
- Debug app target builds successfully.
- Core CTest suite passes: 1 executable, 0 failures.
- Engine integration target builds successfully.
- Plugin registry suite passes: 32 assertions in 4 test cases.
- Package/plugin-state regression passes: 13 assertions in 1 test case.
- Visual smoke showed the right-docked Browser Panel with Plugins/Scales tabs, readable scan status, plugin list, and the existing overlay Plugin Browser button still present.

Known issues:
- The Scales tab was preserved by embedding the existing component; no separate automated UI drag test exists for scale drops.
- Project-file rows currently provide typed drag payloads only; audio/MIDI import drop behavior is left to the later Browser Project Files prompt.
- The integration test binary still prints the known non-fatal JUCE debug assertion during Tracktion initialization.

## Ableton Mixer Prompt 07 - Horizontal Mixer Track Headers

Summary:
- Upgraded `TrackListComponent` from a painted list into lane-aligned mixer header row components.
- Preserved track name, assigned instrument/device summary, plugin drag/drop assignment, and MIDI ARM behavior.
- Added per-track mixer controls:
  - Volume slider.
  - Precise dB text field using the shared `MixerMath` parser/formatter, including `-inf`/`off` support.
  - Pan slider with a visual center mark and compact pan readout.
  - Track Activator button (`On`/`Off`) backed by `MixerStrip::active`.
  - Solo button with normal multi-solo behavior.
  - Realtime meter bar from `PlaybackEngine::getMeterSnapshot()`.
- Added compact routing dropdowns for Audio From, Audio To, MIDI From, and MIDI To.
  - Choices are generated from the core routing model and filtered through `validateProjectRouting`.
  - Invalid/cyclic route selections are rejected by `SetTrackRoutingCommand` and surfaced as user warnings.
- Mixer and routing edits use existing core commands (`SetTrackMixerStripCommand`, `SetTrackRoutingCommand`) so undo/redo and playback-dirty marking remain centralized.
- Widened the left track-header column so the compact horizontal mixer controls can fit without overlapping the timeline.

Commands run:
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `ctest --preset tests --output-on-failure`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- App launch smoke with screenshot captured at `/tmp/theorysequencer-prompt07-track-mixer-headers.png`

Result:
- Debug app target builds successfully.
- Core CTest suite passes: 1 executable, 0 failures.
- Engine integration target builds successfully.
- Plugin registry suite passes: 32 assertions in 4 test cases.
- Package/plugin-state regression passes: 13 assertions in 1 test case.
- Real meter integration passes: 8 assertions in 1 test case.
- Visual smoke showed lane-aligned compact mixer headers with volume, dB, pan, activator, solo, ARM, routing dropdowns, and Browser Panel still visible.

Known issues:
- Mixer header control interaction was visually smoke-tested but not exercised by automated UI click/drag automation.
- MIDI routing choices are stored/validated in the core model, but Tracktion playback sync still logs that MIDI routing is not engine-mapped yet.
- The integration test binary still prints the known non-fatal JUCE debug assertion during Tracktion initialization.

## Ableton Mixer Prompt 08 - Track Creation Context Menus And Drag-To-Create

Summary:
- Added centralized AppServices helpers for transactional track creation:
  - `insertTrack(TrackType)`
  - `createTrackFromPlugin`
  - `createTrackFromPluginStableId`
  - `createAudioTrackFromFile`
  - `createMidiTrackFromFile`
- Added app-level selected-track state for created/selected tracks, also updated when a clip is selected for recording/opening.
- Context menus in empty timeline and track-list space now offer:
  - Insert MIDI Track
  - Insert Audio Track
  - Insert Return Track
- Drag-to-create behavior:
  - Instrument plugin payloads create a MIDI track named after the plugin, with both legacy instrument reference and instrument device slot populated.
  - Audio-effect plugin payloads create an Audio track with that effect in its device chain.
  - Audio file payloads create an Audio track with a default 4-bar audio clip referencing the file.
  - MIDI file payloads report a clear unsupported-operation warning and create no track.
- Existing plugin drops onto an existing track header still assign instruments or append audio effects.
- Timeline drag/drop still preserves the existing scale-lane `tsq-scale:` payload behavior and now shows a subtle empty-track creation preview for valid plugin/file payloads.
- Track creation is command-backed through a single preconfigured `AddTrackCommand`, so failed validation happens before project mutation and undo removes the created track in one step.

Commands run:
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `ctest --preset tests --output-on-failure`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- App launch smoke with screenshot captured at `/tmp/theorysequencer-prompt08-track-creation.png`

Result:
- Debug app target builds successfully.
- Core CTest suite passes: 1 executable, 0 failures.
- Engine integration target builds successfully.
- New track-create integration test passes: 28 assertions in 1 test case.
- Package/plugin-state regression passes: 13 assertions in 1 test case.
- Plugin registry suite passes: 32 assertions in 4 test cases.
- Real meter integration passes: 8 assertions in 1 test case.
- Visual smoke showed track headers, timeline, Browser Panel, and lower piano-roll area still fitting after the creation workflow changes.

Known issues:
- Context-menu and drag/drop gestures were covered through AppServices tests and visual smoke, but not by automated UI click/drag automation.
- MIDI file import is intentionally unsupported until a MIDI importer exists; drops surface a warning and do not mutate the project.
- Audio file drag-created clips use a conservative default 4-bar length until Prompt 11 adds full audio import/waveform duration handling.
- The integration test binary still prints the known non-fatal JUCE debug assertion during Tracktion initialization.

## Ableton Mixer Prompt 09 - Lower Detail Area Toggle And Device Chain View

Summary:
- Added a lower-pane mode model with visible `Piano Roll` and `Device Chain` switch buttons.
- Added global Shift+Tab handling to toggle the lower detail editor between Piano Roll and Device Chain while preserving the existing piano-roll command surface.
- Added `DeviceChainComponent`:
  - Displays the selected track's left-to-right signal flow.
  - Shows MIDI, Audio, Return, and Master source blocks with compatible chain labels.
  - Renders fixed-size device faceplates with enable/bypass state, plugin name, plugin kind, channel/state summary where available, and `Edit` buttons.
  - Shows legacy assigned instruments that have not yet been migrated into explicit device slots; editor opening is supported, while bypass stays disabled unless a command-backed slot exists.
  - Accepts compatible Browser Panel plugin drops onto the selected track/device-chain area.
- Added AppServices methods for command-backed device bypass and selected track plugin editor opening.
- Added playback-engine API for:
  - Opening a project device plugin editor by track ID and device-slot ID.
  - Applying live bypass directly to an already-loaded plugin when the playback graph is current.
- Hardened project plugin editor lifetime:
  - Full playback sync/shutdown now hides project plugin editor windows before clearing live plugin pointers.
  - Device editor opening stays behind the playback-engine abstraction; UI does not instantiate plugins during paint/layout.
- Default project creation and project load now keep a selected track when possible so the Device Chain view has deterministic selection state.

Commands run:
- `cmake --build --preset debug --target tsq_app tsq_engine_integration_tests`
- `ctest --preset tests --output-on-failure`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests`

Result:
- Debug app and app-enabled integration targets build successfully.
- Core CTest suite passes: 1 executable, 0 failures.
- Track-create integration passes: 30 assertions in 1 test case.
- Package/plugin-state regression passes: 13 assertions in 1 test case.
- Plugin registry suite passes: 32 assertions in 4 test cases.
- Real meter integration passes: 8 assertions in 1 test case.
- Full app-enabled integration binary passes: 440 assertions in 8 test cases.

Known issues:
- Shift+Tab, visible mode switching, plugin-editor buttons, and Browser-to-Device-Chain drops still need hands-on GUI/manual QA because they are not covered by automated UI click/drag automation.
- The integration test binary still prints the known non-fatal JUCE debug assertion during Tracktion initialization.

## Ableton Mixer Prompt 10 - Device Chain Drag/Drop, Hot-Swapping, Reordering, And State Safety

Summary:
- Added command-layer support for explicit device replacement:
  - `ReplaceTrackDeviceCommand`
  - Undo restores the previous slot, including plugin reference, bypass, and state-file association.
- Added `CommandStack::rollbackLastExecuted()` so AppServices can undo a just-applied device edit without leaving failed playback-sync work in the redo stack.
- Added AppServices device-chain editing APIs:
  - Insert Browser plugin into a chain at a requested index.
  - Replace/hot-swap a device by stable plugin ID.
  - Move a device to a new chain index.
  - Remove a device.
  - Roll back append/insert/replace/remove/reorder/bypass edits if playback sync fails.
- Hardened hot-swap state recovery:
  - Tracktion engine now keeps a last-known project plugin-state cache keyed by track/slot and plugin identity.
  - Undoing a hot-swap can restore the previous plugin's most recently captured state even after the replacement plugin has been loaded.
  - The cache is cleared when the project plugin-state directory changes, avoiding cross-project state bleed.
- Upgraded `DeviceChainComponent` editing workflows:
  - Browser plugin drop into empty chain or gaps inserts at that position.
  - Browser plugin drop onto an existing command-backed faceplate replaces/hot-swaps that device.
  - Dragging command-backed device faceplates reorders within the same track.
  - Faceplates include an `X` remove affordance.
  - Green insertion and replacement previews show the drop target.
- Invalid chain edits still go through core validation and surface clear AppServices warnings, including unsupported plugin kind, missing plugin, second instrument conflicts, and effect-before-instrument cases.

Commands run:
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests`

Result:
- Debug app target builds successfully.
- Core test target builds successfully.
- Core CTest suite passes: 1 executable, 0 failures.
- Engine integration test target builds successfully.
- Focused integration suites pass:
  - Track creation: 30 assertions in 1 test case.
  - Package/plugin-state: 13 assertions in 1 test case.
  - Plugin registry: 32 assertions in 4 test cases.
  - Metering: 8 assertions in 1 test case.
- Full engine integration binary passes: 464 assertions in 8 test cases.

Known issues:
- Device Chain drag/drop, faceplate hot-swap, internal reorder, remove-button interaction, and plugin-load rollback need hands-on GUI/manual QA because UI drag/drop automation is not yet present.
- Replacement state migration is intentionally conservative: state-file association is preserved only when the replacement resolves to the same plugin identity; otherwise the old slot remains recoverable through undo and engine last-known-state cache.
- Engine integration runs still emit the pre-existing JUCE debug assertion in `juce_AudioPluginFormatManager.cpp:79`; it did not fail the tests.

## Ableton Mixer Prompt 11 - Audio Tracks, Audio File Import, Playback, And Waveform Rendering

Summary:
- Extended timeline clip commands to support audio clips as first-class undoable arrangement clips:
  - `AddClipCommand` can add MIDI or audio clips.
  - `MoveClipCommand`, `ResizeClipCommand`, and `DeleteClipCommand` now operate on either MIDI or audio clips based on the selected clip ID/kind.
  - Audio clip command tests cover add, move, resize, delete, undo/redo, and same-track overlap rejection.
- Upgraded timeline audio-clip workflows:
  - Audio clips participate in hit testing, selection, marquee selection, drag move, right-edge resize, delete, copy, and paste.
  - MIDI-only actions remain guarded: double-click/open and chord-globalize target MIDI clips, while audio selection stays in the timeline.
  - Timeline auto-extension and generated pasted-clip IDs now account for both MIDI and audio clips.
- Added OS audio-file drag/drop into empty timeline space:
  - Supported audio files create a new Audio track through the existing AppServices import path.
  - Unsupported or misplaced drops report user-facing warnings and avoid partial project mutations.
- Added `WaveformCache` for audio clip overview drawing:
  - Uses JUCE thumbnails and thumbnail change callbacks to render waveform overviews in the timeline without blocking paint.
  - Missing files draw an explicit missing-audio placeholder in the clip body.
  - The JUCE audio utility dependency is private to `tsq_ui` so the app does not compile duplicate JUCE audio processor modules.
- Added reference-only waveform-cache metadata to project JSON:
  - `audioAssets.waveformCache.entries` now lists unique audio sources with source ID, path, display name, embedded flag, and cache key.
  - Existing package directory creation for `waveform-cache` remains in place.
- Hardened audio import validation in AppServices:
  - Unsupported extensions are rejected with an `Audio import failed: unsupported audio format` warning.

Commands run:
- `cmake --build --preset tests --target tsq_core_tests`
- `cmake --build --preset debug --target tsq_app`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`

Result:
- Core test target builds successfully.
- Debug app target builds successfully.
- Core CTest suite passes: 1 executable, 0 failures.
- Engine integration target builds successfully.
- Focused non-VST integration suites pass:
  - Package/import: 13 assertions in 1 test case.
  - Track creation/import: 32 assertions in 1 test case.
  - Plugin registry: 32 assertions in 4 test cases.
  - Metering/audio playback: 8 assertions in 1 test case.

Known issues:
- Manual GUI QA is still needed for OS audio-file drag/drop, timeline audio-clip waveform rendering, audio clip copy/paste, and audio clip move/resize with real project files.
- Waveform analysis is UI-owned and thumbnail-cache based; project JSON records source/cache metadata, but binary waveform cache files are not persisted into `.tseq` packages yet.
- Full engine integration was intentionally not run for this handoff because the user requested avoiding the expensive Synthesizer parameter-wipe regression loop during Prompt 11.
- App/UI builds emit the existing static-library warnings about empty JUCE ARA/LV2 objects and duplicate `tsq_core` linkage; they do not fail the build.

## Ableton Mixer Prompt 12 - Return Tracks, Sends, Bussing, And Master Track UI

Summary:
- Added compact send controls to track headers for valid return-track destinations.
  - Send sliders are command-backed through `SetTrackRoutingCommand`.
  - Send target lists are filtered through the routing validator so feedback-producing return paths are not offered.
  - Dragging a send commits once at drag end, while clicks/double-click reset still commit normally.
- Exposed master-track creation from the timeline and track-header empty-space context menus.
  - The menu item is disabled when the project already has its singleton master track.
  - Timeline and track-header row heights were updated together so clips and mixer headers remain aligned.
- Strengthened core route validation so active sends participate in the audio graph.
  - Missing/non-return send targets are still rejected.
  - Send-based feedback cycles, including return-to-return cycles, are now rejected.
  - `Audio From` routes are also treated as source-to-destination graph edges, including self-route rejection.
- Added a core solo-path helper for return tracks and used it during Tracktion graph sync.
  - A return fed by a soloed source track is treated as part of that source's audible path.
  - Transitive return paths are included.
- Updated the feature inventory for return/master/send behavior and the remaining mixer limitations.

Commands run:
- `cmake --build --preset tests --target tsq_core_tests`
- `cmake --build --preset debug --target tsq_app`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`

Result:
- Core tests passed.
- Debug app target rebuilt successfully.
- Focused non-VST integration checks passed.

Known issues:
- The existing JUCE audio plugin format-manager assertion still appears during Tracktion integration-test startup.
- The existing static-library warnings for empty JUCE ARA/LV2 objects and duplicate `tsq_core` linkage still appear.
- The Synthesizer/VST parameter-wipe stress harness was intentionally not run for this prompt, per user request.

## Ableton Mixer Prompt 13 - Automation Core, Engine Binding, And UI Lanes

Summary:
- Added a core automation playback snapshot layer for querying lane values at a musical time.
  - Volume and pan use shared mixer value mapping helpers.
  - Send, device-bypass, and plugin-parameter targets validate against the current project before playback binding.
  - Hidden lanes still participate in playback, while invalid targets are skipped deterministically.
- Bound automation to the Tracktion playback adapter.
  - The engine applies volume, pan, send, plugin-parameter, and device-bypass automation from the control/message path on sync, playback start, playhead moves, and a 30 Hz playback timer.
  - Aux-send plugins are indexed by route so send automation drives real Tracktion send gain/mute state.
  - Plugin parameters are matched by stable parameter ID where available, with numeric index fallback.
- Added timeline automation lanes beneath track clip rows.
  - Track context menus can show/hide volume, pan, send, and scanned plugin-parameter lanes.
  - Clicking a lane creates or selects an automation point, dragging moves it with timeline snapping, and Delete/Backspace removes the selected point.
  - Lane edits use `SetTrackAutomationLaneCommand`, so undo/redo and serialization use the existing command/project path.
- Added deterministic target invalidation for device edits.
  - Removing a device removes that slot's plugin-parameter and bypass automation lanes, with undo restoration.
  - Replacing a device removes stale plugin-parameter lanes for the slot while preserving slot-bypass automation.
- Updated the feature inventory and audio-thread audit to document automation behavior, timing, and deferred sample-accurate automation work.

Commands run:
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`

Result:
- Core tests passed.
- Debug app target rebuilt successfully.
- Focused non-VST integration checks passed.

Known issues:
- Automation timing is currently Tracktion-managed/control-rate automation from the message/control path, not sample-accurate custom DSP automation.
- Plugin parameter lane menus depend on scanned registry metadata; plugins without cached parameter metadata will not expose a rich parameter menu yet.
- Automation copy/paste, marquee multi-point editing, interpolation editing UI, and full GUI manual QA are still pending.
- The Synthesizer/VST parameter-wipe stress harness was intentionally not run for this prompt, per user request.

## Ableton Mixer Prompt 14 - Browser Project Files, Audio And MIDI Import Polish

Summary:
- Added a core Standard MIDI File importer beside the existing MIDI exporter.
  - Supports PPQ-based MIDI file formats 0 and 1.
  - Converts source PPQ timing into TheorySequencer's 960 PPQ tick grid.
  - Handles note-off messages, note-on velocity zero as note-off, running status, meta events, SysEx skipping, and corrupt/truncated file failures.
  - Merges imported MIDI tracks into one MIDI clip for the drag-to-create workflow.
- Wired `AppServices::createMidiTrackFromFile()` to the importer.
  - Supported `.mid` and `.midi` files now create a new MIDI track with one imported clip through the command stack.
  - Unsupported, missing, corrupt, or SMPTE-time MIDI files report user-facing warnings and do not create partial tracks.
  - Existing Browser Panel and timeline project-file drop paths now get real MIDI import automatically.
- Improved Browser Panel project-file rows.
  - Package scanning still enumerates VST3 bundles, audio files, and MIDI files with a bounded cap.
  - Referenced audio assets from the current project are also listed, including files outside the `.tseq` package.
  - File rows show type labels plus file size or missing-reference metadata and remain draggable via typed project-file payloads.
- Updated feature documentation for Browser MIDI drops, project-file metadata, and MIDI import/export coverage.

Commands run:
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `cmake --build --preset debug --target tsq_app`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`

Result:
- Core tests passed.
- Debug app target rebuilt successfully.
- Focused track-creation/import integration check passed, including corrupt MIDI rejection and valid MIDI import.

Known issues:
- MIDI import preserves musical note timing but does not import MIDI tempo or time-signature maps into the song structure.
- Format-2 and SMPTE-time MIDI files are rejected with warnings.
- Browser audio preview is not implemented in this prompt; file rows provide metadata and drag-to-import instead.
- Audio import remains reference-only and relies on the existing Tracktion/JUCE playback and waveform paths for decode behavior.
- The Synthesizer/VST parameter-wipe stress harness was intentionally not run for this prompt, per user request.

## Ableton Mixer Prompt 15 - UI Cohesion, Accessibility, Keyboard Workflow, And Visual Polish

Summary:
- Added an app-level JUCE `TooltipWindow` so existing and new control tooltips are visible throughout the workspace.
- Added Escape-key handling at the main workspace level.
  - Escape closes Audio Settings, Plugin Browser, and Diagnostics overlays.
  - When no overlay is open, Escape releases focused child controls back to the main workspace.
- Added accessibility titles/descriptions across the integrated mixer/browser/device UI.
  - Transport controls now expose explicit names for compact controls such as Return, Record, Quantize, MIDI input, Project, Audio, and Plugins.
  - Track-header controls update their accessible names with the current track name, so repeated volume/pan/routing/send controls are distinguishable.
  - Browser Panel, Device Chain, lower editor tabs, timeline, track headers, status bar, and overlay panels now expose panel/control descriptions.
- Improved empty and no-results states.
  - Browser Panel distinguishes no plugins, no project files, no project package, and no search matches.
  - Timeline and track-list empty states now point users toward adding tracks or dropping plugins/audio/MIDI files.
  - Device Chain distinguishes no selected track from an empty chain that accepts compatible plugin drops.
- Updated the feature inventory with the new tooltip, accessibility, Escape, and empty-state behavior.

Commands run:
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`

Result:
- Core tests passed.
- Debug app target rebuilt successfully.
- Focused non-VST integration checks passed.

Known issues:
- No automated screen-reader or visual screenshot audit was performed in this pass; a hands-on macOS accessibility/UX pass should still verify tab order, VoiceOver labels, and narrow-window layout.
- This pass intentionally avoids changing musical selection semantics for Escape; it only closes overlays or clears keyboard focus.
- The Synthesizer/VST parameter-wipe stress harness was intentionally not run for this prompt, per user request.

## Ableton Mixer Prompt 16 - Performance, Realtime Safety, And Audio Quality Hardening

Summary:
- Audited current realtime boundaries for mixer/device/automation paths and updated the audio-thread audit.
  - TheorySequencer still does not implement a custom audio render callback; Tracktion owns render execution.
  - Project traversal, plugin load/state restore, package IO, MIDI/audio materialization, metering setup, and automation snapshot queries remain on the message/control path.
- Reduced Browser Panel UI-thread churn during plugin scans.
  - Split plugin-cache refresh from full Browser refresh.
  - The plugin-scan timer now refreshes cached plugin rows/status only.
  - Project package recursive enumeration and scale-palette refresh now happen on explicit/full Browser refresh rather than four times per second during scans.
- Reduced global UI timer churn.
  - Removed the idle Browser repaint from the 24 Hz main UI timer.
  - Lower Detail Editor now refreshes only the visible sub-editor: Piano Roll when visible, Device Chain when visible.
- Reduced automation control-path work for sessions without automation.
  - Tracktion adapter now records whether the prepared project contains automation lanes.
  - The 30 Hz automation playback timer only runs during playback when automation lanes exist.
  - `applyAutomationAt()` exits before snapshot traversal for projects without automation lanes.
  - Added a debug assertion that the prepared automation-lane gate matches the synced project snapshot.

Commands run:
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`

Result:
- Core tests passed.
- Debug app target rebuilt successfully.
- Focused non-VST integration checks passed.

Known issues:
- `syncProject()` still performs synchronous Tracktion edit preparation on the message/control path; this is safe for audio-thread rules but can still become slow for very large sessions.
- Automation remains Tracktion-managed/control-rate automation, not sample-accurate custom DSP automation.
- Browser project-file enumeration is still synchronous on explicit refresh/open/save paths, bounded by the existing project-file cap; a background indexer remains a future scalability improvement.
- No manual large-session stress pass or screenshot/UX pass was performed in this environment.
- The Synthesizer/VST parameter-wipe stress harness was intentionally not run for this prompt, per user request.

## Ableton Mixer Prompt 17 - Complete Regression Test Suite And Manual QA Checklist

Summary:
- Expanded the release manual QA checklist into a full end-to-end acceptance sweep.
  - Coverage now includes launch/baseline behavior, plugin scanning, Browser Panel tabs, track creation, Device Chain workflows, floating plugin GUI state, clip and piano-roll editing, mixer playback, returns/sends/master routing, automation, audio/MIDI import, project package recovery, keyboard/UI smoke tests, and MIDI export.
  - The Synthesizer VST state persistence scenario remains documented as a targeted release/investigation pass, not a routine lightweight regression.
- Added a non-VST app integration regression for imported media package round trips.
  - Creates an imported audio track and imported MIDI track through `AppServices`.
  - Saves to a `.tseq` package.
  - Reloads through `ProjectPackage::loadWithWarnings`.
  - Verifies audio clip source metadata, imported MIDI note pitches/timing/durations/velocities, and clean package-load warnings while the source audio file still exists.
- Updated feature documentation to describe the broader integration coverage and release-readiness checklist.

Release-readiness summary:
- Core model/command/serialization coverage remains green through the headless Catch2 suite.
- Focused app integration checks cover typed and drag-created tracks, imported audio/MIDI creation, imported-media package persistence, package plugin-state placeholders, and real metering paths.
- The app target remains buildable from the debug preset.
- The repo now contains a single manual QA checklist that can be used to run the full Ableton-style mixer workflow end to end.

Commands run:
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_app`

Result:
- Core tests passed.
- Debug app target was already up to date and buildable.
- Focused non-VST integration checks passed.

Known issues:
- Manual hands-on QA was not performed in this environment; `docs/MANUAL_QA_CHECKLIST.md` is ready for that pass.
- Full integration-test execution was intentionally avoided here because it includes the Synthesizer/VST state harness; the user requested that extensive parameter-wipe testing not be run during prompt-pack continuation.
- Focused app integration runs still print the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion startup, but the selected non-VST tests pass.

## Ableton Mixer Prompt 18 - Final Product Integration Review

Summary:
- Performed the final Ableton-style mixer integration review against all 14 Prompt 18 requirements.
- Added `docs/ABLETON_MIXER_FINAL_REVIEW.md` with requirement-by-requirement status, evidence, automated verification, manual QA status, and remaining risks.
- Updated feature and known-issue docs to explicitly distinguish sidechain-ready model/persistence support from true sidechain audio graph mapping.
- Verified the shipped mixer workflow support across Browser Panel, scale preservation, drag/drop service paths, mixer track headers, context menus, lower Piano Roll/Device Chain toggle, device-chain workflows, VST3 registry/hosting persistence paths, automation, returns/sends/master, audio clips/waveforms, project migration, audio-thread boundaries, and preserved DAW/theory workflows.

Requirement status:
- Requirements 1-7: Shipped with code evidence for Browser Panel, Scales tab preservation, drag/drop service paths, mixer headers, context menus, lower editor toggle, and Device Chain workflows.
- Requirement 8: Shipped for VST3 scanning/hosting/state/playback with the usual third-party plugin compatibility risk.
- Requirement 9: Shipped for volume, pan, send, bypass, and matched VST3 parameter automation, with Tracktion-managed/control-rate timing rather than sample-accurate custom DSP automation.
- Requirement 10: Shipped for returns, sends, bussing, master track, and sidechain-ready routing references; true sidechain audio graph mapping remains a known limitation.
- Requirements 11-14: Shipped for audio tracks/waveforms/playback, project save/load/migration, current realtime-safety boundaries, and preserved DAW/theory workflows, with manual GUI QA still required before release.

Commands run:
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-create]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_app`

Result:
- Core tests passed.
- Focused non-VST integration checks passed.
- Debug app target was already up to date and buildable.

Manual QA status:
- No hands-on GUI/manual QA pass was performed in this environment.
- `docs/MANUAL_QA_CHECKLIST.md` remains the release checklist for Browser drag/drop, context menus, floating editors, plugin compatibility, package recovery, and narrow-window layout.

Known risks:
- App-enabled integration startup still prints the existing JUCE assertion in `juce_AudioPluginFormatManager.cpp:79`; selected non-VST tests pass afterward.
- True sidechain audio routing is not engine-mapped yet.
- Automation is control-rate/timer-driven rather than sample-accurate.
- `syncProject()` remains synchronous on the message/control path.
- The Synthesizer/VST parameter-wipe stress harness was intentionally not run for this prompt, per user request.

Follow-up needed:
- Run the full manual QA checklist before a release candidate.
- Investigate the JUCE plugin-format startup assertion.
- Implement true sidechain audio graph mapping if sidechain processing becomes a user-facing feature, rather than only a sidechain-ready route reference.
- Revisit sample-accurate automation and asynchronous project preparation for larger sessions.

## Prompt 55 - Add Synthesizer VST State Regression Harness

Summary:
- Added an app-enabled integration test target for the persistent Synthesizer VST reset bug.
- The regression test uses the real hosted `Synthesizer.vst3` when installed:
  - Assigns Synthesizer to `track-1`.
  - Opens the plugin editor.
  - Caches the default hosted-parameter state.
  - Edits `Osc1 Mod Amount` or a fallback movable parameter.
  - Creates a MIDI clip and MIDI note through the real command stack.
  - Syncs the playback project, returns to zero, starts/stops playback, and asserts the edited parameter remains unchanged after each step.
- Added Tracktion-engine debug hooks for the harness to inspect and set loaded plugin parameters numerically, avoiding screenshot-based verification.
- Fixed a stale-cache edge in the parameter reassertion system:
  - Before reasserting observed parameters, the engine now force-refreshes the live plugin parameter snapshot.
  - This prevents a default snapshot captured before a user edit from being reapplied during the next dirty command.
- Stopped the deferred parameter-restore timer during engine shutdown to avoid teardown callbacks against deleted Tracktion/plugin state.

Commands run:
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `out/build/debug/tests/tsq_engine_integration_tests --reporter console --success`
- `ctest --test-dir out/build/debug -R tsq_engine_integration_tests --output-on-failure`
- `cmake --build --preset debug`
- `cmake --build --preset tests && ctest --preset tests`
- `ctest --test-dir out/build/debug -R tsq_engine_integration_tests --output-on-failure --repeat until-fail:5`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.
- Synthesizer VST3 integration regression passed directly and through CTest.
- The Synthesizer integration regression passed 5 repeated CTest runs without failure.

Known issues:
- The integration test skips on machines without `Synthesizer.vst3`; the pure core test preset remains plugin-free.

## Prompt 56 - Make VST Reset Watchdog Actively Restore Per-Parameter Defaults

Summary:
- Followed up after the user reproduced the VST reset despite the initial green Synthesizer integration harness.
- Found the missing case in the automated test:
  - The previous watchdog kept a good cached parameter snapshot but only rejected suspicious all-default observations.
  - It did not actively restore when a reset happened after the short retry window.
  - It also only detected whole-plugin default resets, missing narrower cases where an edited parameter returns to its own default while other parameters remain non-default.
- Strengthened the engine-side watchdog:
  - Observed plugin snapshots now also remember each parameter's default value.
  - During the protected edit window, if a previously edited parameter snaps back to its default, the engine immediately restores the prior observed snapshot.
  - The protected window now lasts 15 seconds after a guarded piano-roll/dirty edit, with deferred retries extended to 16 callbacks at 125 ms.
- Strengthened the integration regression:
  - After creating the MIDI note, the test deliberately forces the edited Synthesizer parameter back to its default after the retry window.
  - The test then verifies that the observer restores the edited value before sync, return-to-zero, and playback.

Commands run:
- `cmake --build --preset debug --target tsq_engine_integration_tests && out/build/debug/tests/tsq_engine_integration_tests --reporter console --success`
- `ctest --test-dir out/build/debug -R tsq_engine_integration_tests --output-on-failure --repeat until-fail:5`
- `cmake --build --preset debug`
- `cmake --build --preset tests && ctest --preset tests`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.
- The strengthened Synthesizer VST3 regression passed directly and passed 5 repeated CTest runs.

## Prompt 57 - Track Exact Synthesizer Oscillator Controls Through Parameter Listener

Summary:
- Followed up after the user confirmed the reset still happened and identified the exact Synthesizer controls being changed:
  - Oscillator 1 Modulator Amount
  - Oscillator 1 Wavefolder Amount
  - Oscillator 1 Wave Shape
- Expanded the Synthesizer integration regression to edit and assert all three corresponding hosted parameters:
  - `Osc1 Mod Amount`
  - `Osc1 Wavefold Amount`
  - `Osc1 Carrier Wave`
- Added a live JUCE parameter listener in the Tracktion engine adapter:
  - The listener attaches to the current project/loaded plugin parameters after plugin load, editor open, and project sync.
  - It detaches before full rebuild/shutdown.
  - Parameter-change callbacks schedule a message-thread snapshot refresh so the cache learns real editor edits immediately instead of relying only on timer polling.
- Kept the active per-parameter reset watchdog from Prompt 56, now backed by immediate parameter-change learning.

Commands run:
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `out/build/debug/tests/tsq_engine_integration_tests --reporter console --success`
- `ctest --test-dir out/build/debug -R tsq_engine_integration_tests --output-on-failure --repeat until-fail:5`
- `cmake --build --preset debug`
- `cmake --build --preset tests && ctest --preset tests`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.
- The Synthesizer VST3 regression passed directly with all three user-named controls.
- The expanded Synthesizer integration regression passed 5 repeated CTest runs.

## Prompt 58 - Add VST State Diagnostics and Piano-Roll Click Regression

Summary:
- Continued the persistent Synthesizer reset investigation after the reset still reproduced manually.
- Identified and fixed a dangerous behavior in the protected parameter restore path:
  - The deferred restore timer was unconditionally reapplying the last observed snapshot.
  - If the last observed snapshot was still defaults, a piano-roll click/edit could make the protection code itself reset the plugin.
  - The restore path now reconciles current and observed snapshots, only restoring when the current state matches the specific suspicious pattern of a previously edited parameter returning to its default.
  - Non-suspicious current live values are learned instead of overwritten.
- Added a reusable diagnostics trail:
  - `Logger` can now write to an automatic disk log.
  - App sessions write to `~/Library/Application Support/TheorySequencer/diagnostics.log` on macOS.
  - Diagnostics panel output includes the log path and a live plugin-parameter summary.
  - App/piano-roll events now trace plugin state around dirty marking, restore scheduling, sync, return-to-zero, playback start, mouse-down, double-click, and piano-roll commands.
  - Plugin-state traces show the exact Synthesizer controls from the manual repro plus a generic first-non-default summary for future plugins.
- Expanded the Synthesizer integration regression:
  - Hosts a real `PianoRollComponent` in a JUCE `DocumentWindow`.
  - Emulates a single piano-roll click and a double-click note entry through real `MouseEvent` payloads.
  - Verifies `Osc1 Mod Amount`, `Osc1 Wavefold Amount`, and `Osc1 Carrier Wave` survive the UI path, direct note command, sync, return-to-zero, playback, and a deliberate reset-to-default challenge.

Commands run:
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `out/build/debug/tests/tsq_engine_integration_tests --reporter console --success`
- `out/build/debug/tests/tsq_engine_integration_tests --reporter compact 2>&1 | rg "JUCE Assertion failure|All tests passed|test cases|assertions"`
- `ctest --test-dir out/build/debug -R tsq_engine_integration_tests --output-on-failure`
- `cmake --build --preset debug`
- `cmake --build --preset tests && ctest --preset tests`
- `ctest --test-dir out/build/debug -R tsq_engine_integration_tests --output-on-failure --repeat until-fail:5`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.
- The Synthesizer VST3 integration regression passed directly and through CTest.
- The repeated Synthesizer VST3 integration regression passed 5/5 runs.
- The assertion scan showed only the existing `juce_AudioPluginFormatManager.cpp:79` assertion, not the piano-roll UI harness assertion.

## Prompt 54 - Reassert Cached VST Parameters Around Piano-Roll Edits

Summary:
- Continued the persistent VST reset investigation after the reset was narrowed further: edited plugin settings are wiped as soon as a MIDI note is created.
- Rechecked the immediate note path and kept it free of heavy plugin state reads:
  - `AddNoteCommand` still only mutates the core MIDI note list.
  - Dirty marking remains a dirty flag write, with no opaque VST state chunk capture.
- Added a lightweight live parameter observer:
  - The main UI timer samples exposed plugin parameter values from loaded project instruments.
  - Piano-roll mouse/edit boundaries reassert the last observed parameter values immediately and with short delayed retries.
  - If an edited non-default snapshot is followed by an all-default snapshot during the protected edit window, the cache keeps the older edited snapshot instead of learning the suspected reset state.

Commands run:
- `cmake --build --preset debug`
- `cmake --build --preset tests`
- `ctest --preset tests`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.

Known issues:
- Manual VST editor verification is still needed because the automated tests do not host an interactive third-party VST editor.

## Prompt 53 - Remove Dirty-Time VST State Writes and Add Parameter Snapshot Restore

Summary:
- Continued the persistent VST reset investigation after the reset still reproduced when the first note was added to an existing MIDI clip.
- Re-audited the first-note path:
  - `AddNoteCommand` mutates only the core project note list.
  - Piano-roll framing/repaint helpers do not touch playback.
  - The command-stack dirty callback was the only immediate plugin-touching path.
- Superseded the Prompt 52 dirty-time preservation approach:
  - Removed `PlaybackEngine::preserveLivePluginState()`.
  - `AppServices::markPlaybackProjectDirty()` is again a pure dirty flag write.
  - Live VST state capture no longer rewrites Tracktion's plugin ValueTree as a side effect.
- Hardened the remaining sync/play protection:
  - Live project plugin snapshots now include the opaque VST state chunk plus exposed normalized parameter values.
  - Sync/play restore now reapplies both the state chunk and parameter values, covering plugins whose state chunk is stale/default but whose editor parameters still report the edited sound.
  - Project save still writes only non-empty opaque state chunks; parameter-only fallback snapshots are not persisted as empty plugin-state files.

Commands run:
- `cmake --build --preset debug`
- `cmake --build --preset tests`
- `ctest --preset tests`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.

Known issues:
- Manual VST editor verification is still needed because the automated tests do not host an interactive third-party VST editor.

## Prompt 52 - Preserve VST State When Project Becomes Playback-Dirty

Superseded by Prompt 53. Capturing live plugin state from `AppServices::markPlaybackProjectDirty()` turned out to be too invasive for the first-note repro path, so dirty marking is now pure again.

Summary:
- Continued the VST reset investigation after the reset persisted even when piano-roll cursor placement no longer touched the playback engine.
- Temporarily added a playback-engine hook to preserve live project plugin state as soon as the app marks playback dirty. This approach was removed in Prompt 53.
  - `PlaybackEngine::preserveLivePluginState()`
  - `TracktionPlaybackEngine` stores a per-track preserved VST state cache.
  - `AppServices::markPlaybackProjectDirty()` temporarily snapshotted the live VST state before setting `playbackProjectDirty_`; this was removed in Prompt 53.
- Dirty playback sync temporarily preferred the preserved state cache over capturing state later during sync. This approach was removed in Prompt 53.
  - This was intended to capture the edited synth state at note/clip edit time, before a later Tracktion sync or graph refresh could reset the plugin.
- Removed `suspendProcessing(true/false)` around our direct `AudioPluginInstance::getStateInformation()` call.
  - Reading a state chunk should not toggle plugin processing state, especially while debugging a synth reset path.

Commands run:
- `cmake --build --preset debug`
- `cmake --build --preset tests`
- `ctest --preset tests`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.

Known issues:
- Manual VST editor verification is still needed because the automated tests do not host an interactive third-party VST editor.

## Prompt 51 - Keep Piano-Roll Cursor Placement Away from VST State

Summary:
- Continued the VST reset investigation after the repro was narrowed further:
  - VST settings remain correct after creating a MIDI clip.
  - The reset appears when making the first note in the piano roll or moving the piano-roll playhead/paste cursor.
- Removed live VST state capture/restore from plain playback playhead moves.
  - `TracktionPlaybackEngine::setPlayheadPosition` now only sets the transport position.
  - This avoids writing a stale/default state chunk back into the plugin during simple cursor movement.
- Changed piano-roll paste cursor placement so it no longer calls `AppServices::setPlaybackPlayheadPosition`.
  - Piano-roll clicks and double-clicks still move the piano-roll paste cursor.
  - They no longer touch the Tracktion playback transport or VST state.
  - This keeps note creation/double-click interactions from triggering engine-side plugin state writes.

Commands run:
- `cmake --build --preset debug`
- `cmake --build --preset tests`
- `ctest --preset tests`

Result:
- Debug app target rebuilt successfully.
- Headless core tests remained green: 1 CTest executable, 0 failures.

Known issues:
- Manual VST editor verification is still needed because the automated tests do not host an interactive third-party VST editor.
