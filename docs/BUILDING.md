# Building TheorySequencer

TheorySequencer is built with CMake and local, pinned dependency checkouts. The repository does not fetch floating dependency branches during CMake configure.

## Build Modes

- Desktop app builds require JUCE and Tracktion Engine. They can scan/load VST3 plugins and use audio/MIDI hardware.
- Headless core tests require Catch2 only when configured with `TSQ_BUILD_APP=OFF`. These tests are pure unit tests and do not require installed plugins, a plugin scan, an audio interface, or a display server.

## Presets

The repository provides Ninja-based CMake presets:

- `debug`: Debug desktop app build in `out/build/debug`.
- `release`: Release desktop app build in `out/build/release`.
- `tests`: Headless core-test build in `out/build/tests` with `TSQ_BUILD_APP=OFF`.

Commands:

```sh
cmake --preset debug
cmake --build --preset debug

cmake --preset tests
cmake --build --preset tests
ctest --preset tests
```

If Ninja is not available, use ordinary CMake commands with your platform generator and the same cache variables.

## macOS

Prerequisites:

- CMake 3.22 or newer.
- Ninja for presets, or Xcode/Unix Makefiles if using manual generator commands.
- Current Xcode command line tools.
- Local dependency checkouts under `deps/`.

Debug app build:

```sh
cmake --preset debug
cmake --build --preset debug
```

Launch path for the Ninja debug preset:

```sh
out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer
```

Headless tests:

```sh
cmake --preset tests
cmake --build --preset tests
ctest --preset tests
```

Manual Xcode-style configure, if needed:

```sh
cmake -S . -B build-xcode -G Xcode -DTSQ_BUILD_APP=ON -DTSQ_BUILD_TESTS=ON
cmake --build build-xcode --config Debug --target tsq_app
```

## Windows

Prerequisites:

- CMake 3.22 or newer.
- Visual Studio 2022 with the Desktop development with C++ workload.
- Ninja for presets, or the Visual Studio generator for IDE builds.
- Local dependency checkouts under `deps\`.

From a Visual Studio Developer PowerShell:

```powershell
cmake --preset debug
cmake --build --preset debug
```

Manual Visual Studio generator configure, if needed:

```powershell
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 -DTSQ_BUILD_APP=ON -DTSQ_BUILD_TESTS=ON
cmake --build build-vs --config Debug --target tsq_app
```

Headless core tests:

```powershell
cmake --preset tests
cmake --build --preset tests
ctest --preset tests
```

## Linux

Prerequisites:

- CMake 3.22 or newer.
- Ninja for presets, or Makefiles if using manual generator commands.
- GCC or Clang with C++20 support.
- Development packages required by JUCE/Tracktion GUI, audio, and plugin-hosting modules.
- Local dependency checkouts under `deps/`.

Typical Debian/Ubuntu package baseline:

```sh
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  libasound2-dev \
  libfreetype6-dev \
  libfontconfig1-dev \
  libx11-dev \
  libxext-dev \
  libxinerama-dev \
  libxrandr-dev \
  libxcursor-dev
```

Debug app build:

```sh
cmake --preset debug
cmake --build --preset debug
```

Headless core tests:

```sh
cmake --preset tests
cmake --build --preset tests
ctest --preset tests
```

## Test Separation

The pure automated test target is `tsq_core_tests`. It is labeled:

- `unit`
- `pure`
- `headless`

It links only `tsq_core` and Catch2. It should remain free of plugin scans, plugin hosting, audio device access, MIDI hardware, and UI display requirements.

When `TSQ_BUILD_APP=ON`, the debug app build also provides `tsq_engine_integration_tests`. This target hosts the real VST3 backend and is labeled:

- `integration`
- `engine`
- `plugin`
- `vst3`

The current integration regression looks for `Synthesizer.vst3`, opens its editor, edits the hosted Oscillator 1 controls used in the manual repro, creates a MIDI clip, emulates piano-roll single-click and double-click note-entry behavior through JUCE mouse events, syncs playback, returns to zero, starts/stops playback, and verifies that the edited parameters remain intact. It skips when that plugin is not installed.

Debug app sessions also write a focused diagnostics log to:

```sh
~/Library/Application Support/TheorySequencer/diagnostics.log
```

## Useful Manual Commands

Configure an app build without presets:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTSQ_BUILD_APP=ON -DTSQ_BUILD_TESTS=ON
cmake --build build --target tsq_app
ctest --test-dir build --output-on-failure
```

Run the app-enabled Synthesizer/VST3 state regression from the debug preset:

```sh
cmake --build --preset debug --target tsq_engine_integration_tests
ctest --test-dir out/build/debug -R tsq_engine_integration_tests --output-on-failure
```

Configure headless core tests without app dependencies:

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Debug -DTSQ_BUILD_APP=OFF -DTSQ_BUILD_TESTS=ON
cmake --build build-tests --target tsq_core_tests
ctest --test-dir build-tests --output-on-failure
```
