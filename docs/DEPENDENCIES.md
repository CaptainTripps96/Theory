# Dependencies

TheorySequencer uses local dependency checkouts. CMake validates the enabled dependency paths and fails with a clear message when a required checkout is missing.

## Expected Layout

```text
deps/
  JUCE/
  tracktion_engine/
  Catch2/
```

The paths can be overridden at configure time:

```sh
cmake -S . -B build \
  -DTSQ_JUCE_DIR=/path/to/JUCE \
  -DTSQ_TRACKTION_ENGINE_DIR=/path/to/tracktion_engine \
  -DTSQ_CATCH2_DIR=/path/to/Catch2
```

## Current Pinned Checkouts

- JUCE: `8.0.13`, expected at `deps/JUCE`.
- Tracktion Engine: commit `2877b62`, expected at `deps/tracktion_engine`.
- Catch2: `v3.15.0`, expected at `deps/Catch2`.

Use pinned tags or commits when updating dependencies. Do not change configure-time behavior to fetch floating branches automatically.

## JUCE

JUCE is the application, UI, audio-device, and plugin-hosting framework. Desktop app builds require a JUCE checkout with CMake support.

When `TSQ_BUILD_APP=OFF`, JUCE is not required for the current headless core tests.

## Tracktion Engine

Tracktion Engine is the playback/plugin backend and is integrated behind TheorySequencer's own engine abstraction. App builds add Tracktion modules from:

```text
deps/tracktion_engine/modules
```

When `TSQ_BUILD_APP=OFF`, Tracktion Engine is not required for the current headless core tests.

## Catch2

Catch2 provides the automated test runner. Test builds require a Catch2 v3 checkout that provides `Catch2::Catch2WithMain`.

The `tests` preset builds only `tsq_core_tests`, so it requires Catch2 but not JUCE, Tracktion Engine, installed plugins, audio hardware, or MIDI hardware.

## VST3 Requirements

TheorySequencer currently enables VST3 hosting with `JUCE_PLUGINHOST_VST3=1` in the engine target. Manual plugin scanning/loading requires installed VST3 plugins on the test machine.

Common VST3 install locations:

- macOS: `/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/VST3`
- Windows: `C:\Program Files\Common Files\VST3`
- Linux: `/usr/lib/vst3`, `/usr/local/lib/vst3`, and `~/.vst3`

No installed VST3 instrument is required for the pure core test suite.

Before distribution, confirm the current JUCE, Tracktion Engine, Steinberg VST3, and individual plugin-vendor licensing terms for commercial use.

## Commercial Licensing Reminder

The product is intended for commercial release. Plan around commercial licensing for:

- JUCE
- Tracktion Engine
- VST3 SDK and trademark requirements, if applicable to distribution
- Any bundled or recommended third-party plugins, samples, presets, or assets

Do not assume GPL or AGPL licensing is acceptable for the final product unless the product strategy explicitly changes.
