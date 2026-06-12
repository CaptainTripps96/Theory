# TheorySequencer

TheorySequencer is a commercial-quality cross-platform C++20/JUCE/Tracktion Engine MIDI sequencer and future DAW. The initial product focus is a theory-aware MIDI composition workflow built around a custom project model, with Tracktion Engine wrapped behind the app's own playback/audio abstraction layer.

## Prerequisites

- CMake 3.22 or newer.
- A C++20 compiler:
  - macOS: Apple Clang from current Xcode command line tools.
  - Windows: MSVC from Visual Studio 2022.
  - Linux: GCC or Clang with C++20 support.
- Local dependency checkouts placed under `deps/`.

This repository intentionally does not fetch floating dependency branches during configure.

## Expected Dependency Layout

```text
deps/
  JUCE/
  tracktion_engine/
  Catch2/
```

If any of these folders are missing, CMake configuration fails with a clear message. Tracktion Engine is integrated behind TheorySequencer's own playback abstraction layer.

Current local checkouts:
- JUCE `8.0.13`
- Tracktion Engine commit `2877b62`
- Catch2 `v3.15.0`

## Configure

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

## Build

```sh
cmake --build build
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Build Documentation

- Build and preset instructions: `docs/BUILDING.md`
- Dependency layout and licensing notes: `docs/DEPENDENCIES.md`
- Manual plugin/audio QA checklist: `docs/MANUAL_QA_CHECKLIST.md`

## Initial Targets

- `tsq_core`
- `tsq_engine`
- `tsq_ui`
- `tsq_app`
- `tsq_core_tests`
