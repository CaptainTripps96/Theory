# Segment 12 - Plugin Scanning, Plugin Registry, And Parameter Metadata

Date: 2026-06-14

## Scope

Measured plugin registry cache behavior, parameter-heavy plugin metadata, Browser Panel refresh, overlay Plugin Browser refresh, stable-ID lookup, and automation parameter target discovery.

This segment did not run the expensive Synthesizer/VST parameter-wipe stress harness. It also did not perform a live third-party VST scan. Instead, it used deterministic synthetic VST3 metadata so cache, browser, and parameter-menu costs can be measured repeatably.

## User-Visible Symptom Investigated

Plugin-heavy projects and large scanned plugin caches can make browser refreshes and drag/assignment workflows feel sluggish. Parameter metadata increases the cost because a plugin row may only display a name and parameter count, but the UI was copying full `PluginDescription` objects including all parameter descriptions during filtering.

Stable-ID lookup was also linear through the registry. That matters for plugin drag payloads, track-device assignment by stable ID, and automation parameter menu population.

## Baseline Measurements

Added an integration performance probe with:

- 1,000 synthetic VST3 plugins.
- 64 synthetic parameters per plugin.
- 64,000 total parameter records.
- Registry replace/save/load/classification timing.
- 2,860 stable-ID lookups.
- Automation parameter target-building for 13 plugin slots.
- Browser Panel and overlay Plugin Browser refresh against the parameter-heavy cache.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][plugin-metadata][perf]"
```

Filtered baseline before optimization:

| Probe | Baseline |
|---|---:|
| Registry `replaceAll`, 1,000 plugins x 64 params | 24.318 ms |
| Registry save, 1,000 plugins x 64 params | 3,248.654 ms |
| Registry load, 1,000 plugins x 64 params | 3,192.113 ms |
| Registry classify snapshots | 11.709 ms |
| Stable lookup, 143 IDs x 20 repetitions | 285.694 ms |
| Automation parameter target build, 13 slots | 1.821 ms |
| Browser Panel refresh, 1,000 plugins x 64 params | 17.313 ms |
| Plugin Browser refresh, 1,000 plugins x 64 params | 17.745 ms |

Phase observations:

- Stable-ID lookup was the clearest algorithmic hot path.
- Browser filtering copied parameter-heavy plugin records into a second vector.
- Overlay Plugin Browser had the same copy pattern.
- XML save/load of large parameter metadata is expensive and mostly dominated by JUCE XML parse/write behavior.

## Changes Made

- Added `tests/integration/PluginMetadataPerformanceProbeTest.cpp` with the tag `[integration][plugin-metadata][perf]`.
- Added an internal stable-ID index to `PluginRegistry`.
  - `replaceAll()` now builds the index once after normalizing and sorting.
  - `findByStableId()` now uses the index instead of scanning the full vector.
  - `clear()` clears the index.
- Added reserve hints while loading and classifying plugin metadata.
  - Loaded plugin arrays reserve child count.
  - Loaded parameter arrays reserve child count.
  - Instrument/effect snapshot vectors reserve registry size.
- Changed Browser Panel filtering to keep filtered plugin indices instead of copying full plugin descriptions.
- Changed overlay Plugin Browser filtering to keep filtered plugin indices instead of copying full plugin descriptions.
- Changed timeline automation parameter menu population to look up only the track's device plugins by stable ID instead of copying the entire registry.

Files changed:

- `src/engine/plugins/PluginRegistry.h`
- `src/engine/plugins/PluginRegistry.cpp`
- `src/ui/BrowserPanelComponent.h`
- `src/ui/BrowserPanelComponent.cpp`
- `src/ui/PluginBrowserComponent.h`
- `src/ui/PluginBrowserComponent.cpp`
- `src/ui/TimelineComponent.cpp`
- `tests/integration/PluginMetadataPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/12_PLUGIN_SCANNING_PLUGIN_REGISTRY_AND_PARAMETER_METADATA.md`

## Before/After Result

Post-change probe command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][plugin-metadata][perf]"
```

Post-change timings:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| Registry `replaceAll`, 1,000 plugins x 64 params | 24.318 ms | 19.955 ms | about 18% faster |
| Registry save, 1,000 plugins x 64 params | 3,248.654 ms | 3,159.218 ms | about 3% faster |
| Registry load, 1,000 plugins x 64 params | 3,192.113 ms | 3,221.910 ms | about 1% slower/noisy |
| Registry classify snapshots | 11.709 ms | 11.612 ms | about the same |
| Stable lookup, 143 IDs x 20 repetitions | 285.694 ms | 16.899 ms | about 94% faster |
| Automation parameter target build, 13 slots | 1.821 ms | 0.572 ms | about 69% faster |
| Browser Panel refresh, 1,000 plugins x 64 params | 17.313 ms | 8.782 ms | about 49% faster |
| Plugin Browser refresh, 1,000 plugins x 64 params | 17.745 ms | 9.833 ms | about 45% faster |

The largest interaction win is stable lookup and parameter-heavy browser refresh. XML persistence remains expensive for huge parameter caches and should be treated as a larger persistence-format or lazy-metadata problem.

## Hot Paths Found

1. `PluginRegistry::findByStableId()` was linear.
   - This is now indexed by stable ID.

2. Browser filtering copied full plugin records.
   - Both plugin browser UIs now keep filtered indices into the cached plugin snapshot.

3. Timeline automation parameter menu copied the whole registry.
   - It now looks up only the device plugins on the selected track.

4. Plugin cache load did not reserve obvious container sizes.
   - Loaded plugin and parameter vectors now reserve XML child counts.

5. XML save/load of large parameter caches is still expensive.
   - The probe's 64,000 parameters take about 3.2 seconds to save/load in a debug build.

## Verification Run

Completed verification:

- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- Filtered `[integration][plugin-metadata][perf]` probe with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][plugin-metadata][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][browser][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][device-chain][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][commands][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][track-list][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][plugin-state]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][automation][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][sync][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app`.
- Verified matching executable hash:
  - `2b0b580f334bdbd60e14ba700fe8ae1ef095555982ca88f0acd3d4cfe99db065`

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- Existing warning in `VstStateRegressionTest.cpp` about an unused variable when that file is compiled.
- `shasum` emitted the known macOS locale warning before printing matching hashes.

## Remaining Risks

- Parameter-heavy registry XML save/load is still expensive and synchronous.
- `PluginRegistry::plugins()` still returns full plugin copies, so first browser refresh after a registry revision still copies all parameter metadata once.
- `findByStableId()` still returns a full `PluginDescription` copy. That is fine for assignment and small device counts, but a future no-copy callback or lightweight summary API would reduce parameter-copy overhead further.
- Live third-party VST scanning was not run in this segment; scan-worker crash/dead-man behavior remains covered by existing status tests and manual QA.
- Automation parameter menu population is faster, but plugins without cached parameter metadata still cannot expose rich parameter menus.

## Suggested Next Segment

Run Segment 13 - MIDI Input, Recording, Transport, And Playhead Polling.

Reason:
- Plugin cache lookup and parameter-heavy browser refresh are now materially cheaper.
- The next likely source of ordinary editing lag is high-frequency input/playhead polling and recording event processing.

## Performance Segment Handoff

- Segment: 12 - Plugin Scanning, Plugin Registry, And Parameter Metadata
- User-visible symptom investigated: lag from large scanned plugin caches, parameter-heavy browser filtering, stable-ID lookup, and automation parameter menu population.
- Baseline measurements: stable lookup was 285.694 ms for 2,860 lookups; Browser Panel refresh was 17.313 ms; Plugin Browser refresh was 17.745 ms; parameter-heavy XML save/load was about 3.2 seconds.
- Hot paths found: linear stable-ID lookup, full plugin copies during browser filtering, whole-registry copy for automation parameter menus, no reserve hints while loading parameter metadata.
- Changes made: stable-ID index, index-based browser filtering, targeted automation-menu plugin lookup, registry load/classification reserves, plugin metadata performance probe.
- Files changed: `src/engine/plugins/PluginRegistry.*`, `src/ui/BrowserPanelComponent.*`, `src/ui/PluginBrowserComponent.*`, `src/ui/TimelineComponent.cpp`, `tests/integration/PluginMetadataPerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, this report.
- Verification run: debug app build, core tests, full test preset, focused plugin/browser/device/timeline/audio integrations, debug-app sync, and matching executable hash completed.
- Before/after result: stable lookup dropped to 16.899 ms, Browser Panel refresh to 8.782 ms, Plugin Browser refresh to 9.833 ms, and automation parameter target build to 0.572 ms.
- Remaining risks: synchronous XML save/load for large parameter caches, first snapshot copy from `PluginRegistry::plugins()`, no-copy registry lookup API deferred, live scan not rerun.
- Suggested next segment: 13 - MIDI Input, Recording, Transport, And Playhead Polling.
