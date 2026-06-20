# Segment 11 - Audio Files, Waveforms, Project Package IO, And Serialization

Date: 2026-06-14

## Scope

Measured audio import, audio clip serialization/deserialization, project package save/load, missing audio warnings, and timeline waveform/audio-clip painting.

This segment focused on file-heavy workflows that can block editing or make the timeline feel laggy when many audio clips are visible. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

Audio-heavy projects can feel sluggish when the timeline is zoomed out or when a package contains many clips that reference the same audio source.

The largest issue found was visible audio clip painting. In a dense timeline, each tiny audio clip still attempted to draw waveform and text details that were not readable at that zoom level. Package load also repeated filesystem existence checks for the same referenced audio file once per clip.

## Baseline Measurements

Added an integration performance probe with:

- A generated real WAV file.
- Audio import through `AppServices::createAudioTrackFromFile()`.
- A project with 16 audio tracks and 512 audio clips sharing one source file.
- Project JSON serialize/deserialize.
- Package save/load with the source file present.
- Timeline cold and warm paint for the audio project.
- Package load after removing the source file to exercise missing-source warnings.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][audio-package][perf]"
```

Filtered baseline before optimization:

| Probe | Baseline |
|---|---:|
| Import audio file | 1.013 ms |
| Serialize, 16 tracks / 512 audio clips | 62.205 ms |
| Deserialize, 16 tracks / 512 audio clips | 45.009 ms |
| Package save, 16 tracks / 512 audio clips | 65.236 ms |
| Package load, present source | 57.115 ms |
| Timeline audio paint, cold | 526.934 ms |
| Timeline audio paint, warm | 220.062 ms |
| Package load, missing source | 61.683 ms |

Phase observations:

- Timeline paint was the clear hot path.
- Repeated waveform/text/detail drawing on tiny clips dominated dense audio tracks.
- Package load repeatedly checked the same source path across many clips.
- JSON serialize/deserialize was meaningful but not as large as the visible timeline paint cost.

## Changes Made

- Added `tests/integration/AudioPackagePerformanceProbeTest.cpp` with the tag `[integration][audio-package][perf]`.
- Optimized `WaveformCache`:
  - Caches rendered waveform, missing-file, and pending-thumbnail images per source/size/color state.
  - Avoids revalidating file size/modification metadata on every draw; cached sources are revalidated on a short interval.
  - Invalidates rendered waveform images when JUCE thumbnail change callbacks arrive.
- Optimized dense audio clip timeline painting:
  - Skips waveform drawing for tiny audio clips where the waveform cannot be read.
  - Skips clip labels, beat labels, and resize-handle paint below width thresholds.
  - Preserves the colored clip body, compact center line, and selection outline at dense zooms.
- Optimized package missing-audio checks:
  - Caches source availability by resolved path during a package warning pass.
  - Preserves existing per-clip warning behavior while avoiding repeated filesystem checks.
- Optimized audio clip model insertion:
  - Inserts audio clips into the sorted vector with `std::lower_bound`.
  - Checks only neighboring clips for overlap once the vector invariant is sorted/non-overlapping.
- Added serializer reserve hints for known-size arrays:
  - tracks, clips, audio clips, automation lanes/points, MIDI notes, device slots, sends, musical structure regions, tempo/time-signature nodes, scales, pitch-class arrays, and audio asset entries.

Files changed:

- `src/ui/WaveformCache.h`
- `src/ui/WaveformCache.cpp`
- `src/ui/TimelineComponent.cpp`
- `src/core/sequencing/Track.cpp`
- `src/core/serialization/ProjectPackage.cpp`
- `src/core/serialization/ProjectSerializer.cpp`
- `tests/integration/AudioPackagePerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/11_AUDIO_FILES_WAVEFORMS_PROJECT_PACKAGE_IO_AND_SERIALIZATION.md`

## Before/After Result

Post-change probe command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][audio-package][perf]"
```

Post-change timings:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| Import audio file | 1.013 ms | 0.847 ms | about 16% faster |
| Serialize, 16 tracks / 512 audio clips | 62.205 ms | 54.838 ms | about 12% faster |
| Deserialize, 16 tracks / 512 audio clips | 45.009 ms | 36.074 ms | about 20% faster |
| Package save, 16 tracks / 512 audio clips | 65.236 ms | 62.440 ms | about 4% faster |
| Package load, present source | 57.115 ms | 46.807 ms | about 18% faster |
| Timeline audio paint, cold | 526.934 ms | 156.526 ms | about 70% faster |
| Timeline audio paint, warm | 220.062 ms | 52.491 ms | about 76% faster |
| Package load, missing source | 61.683 ms | 41.903 ms | about 32% faster |

The biggest user-facing win is dense audio timeline paint. Warm paint for 512 visible audio clips is now roughly 4.2x faster in the probe.

## Hot Paths Found

1. Dense audio clip paint drew unreadable details.
   - Tiny clips still paid for waveform/text/beat-label/handle drawing.
   - The renderer now uses a compact representation below width thresholds.

2. Waveform rendering repeated thumbnail drawing.
   - `WaveformCache` now caches rendered images for repeated source/size/color states.
   - Thumbnail callbacks invalidate those cached bitmaps when analysis progresses.

3. Waveform source validation touched the filesystem too often.
   - Cache-key metadata is now revalidated on a short interval instead of every clip draw.

4. Package load repeated identical source existence checks.
   - Missing-audio warning generation now checks each resolved path once per load.

5. Audio clip insertion sorted the full vector on each add.
   - Audio clips are now inserted into sorted position directly.

6. Serializer arrays grew without capacity hints.
   - Known-size arrays now reserve capacity before push loops.

## Verification Run

Completed verification:

- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- Filtered `[integration][audio-package][perf]` probe with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][audio-package][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][automation][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][sync][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][device-chain][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-list][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][browser][perf]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app`.
- Verified matching executable hash:
  - `92dbfaab53c02af92a2d34dd2b01c24987d119a3cb3e19456a8c649418b28e2e`

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- `shasum` emitted the known macOS locale warning before printing matching hashes.

## Remaining Risks

- Package save still serializes and writes the whole project JSON synchronously.
- Timeline grid drawing can still dominate cold paints in very large views.
- Waveform thumbnail analysis is still JUCE thumbnail-cache based and UI-owned; there is no explicit background progress UI yet.
- Dense MIDI clip painting was not changed in this segment; the density thresholds were applied only to audio clip detail drawing.
- Package warnings still emit per clip for a missing shared source. The filesystem check is cached, but warning volume can still be high.

## Suggested Next Segment

Run Segment 12 - Plugin Scanning, Plugin Registry, And Parameter Metadata.

Reason:
- File-heavy audio/package paths are now measured and materially cheaper.
- The next likely source of ordinary editing lag is plugin discovery/metadata refresh and plugin parameter list handling.

## Performance Segment Handoff

- Segment: 11 - Audio Files, Waveforms, Project Package IO, And Serialization
- User-visible symptom investigated: lag from dense visible audio clips, waveform drawing, package load warnings, and clip-heavy serialization.
- Baseline measurements: dense timeline audio paint was 526.934 ms cold and 220.062 ms warm; package load was 57.115 ms with present audio and 61.683 ms with missing shared audio.
- Hot paths found: tiny audio clip detail drawing, repeated thumbnail rendering, repeated waveform file metadata checks, repeated package source existence checks, full sort on each audio clip insertion.
- Changes made: audio/package performance probe, waveform rendered-image cache, timed waveform file revalidation, dense audio clip paint thresholds, per-load source availability cache, sorted audio clip insertion, serializer reserve hints.
- Files changed: `src/ui/WaveformCache.*`, `src/ui/TimelineComponent.cpp`, `src/core/sequencing/Track.cpp`, `src/core/serialization/ProjectPackage.cpp`, `src/core/serialization/ProjectSerializer.cpp`, `tests/integration/AudioPackagePerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, this report.
- Verification run: debug app build, core tests, full test preset, focused integration probes, Browser perf probe, debug-app sync, and matching executable hash completed.
- Before/after result: dense timeline audio paint dropped to 156.526 ms cold and 52.491 ms warm; package load dropped to 46.807 ms with present audio and 41.903 ms with missing shared audio.
- Remaining risks: synchronous whole-project package save, grid drawing on cold paints, no explicit waveform background-progress UI, high missing-warning volume.
- Suggested next segment: 12 - Plugin Scanning, Plugin Registry, And Parameter Metadata.
