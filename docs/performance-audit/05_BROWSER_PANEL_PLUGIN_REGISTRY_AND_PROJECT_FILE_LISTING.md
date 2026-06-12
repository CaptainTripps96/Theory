# Segment 05 - Browser Panel, Plugin Registry, And Project File Listing

Date: 2026-06-10

## Scope

Measured and reduced Browser Panel and overlay Plugin Browser refresh cost.

This segment focused on cached plugin registry listing, project package file enumeration, search/filter rebuilds, and scan-status timer behavior. The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

The right Browser Panel can make unrelated project actions feel laggy because `MainComponent` refreshes it after save/open/undo/redo and other project-view updates. If a project package contains many assets, a broad browser refresh could recursively scan the package even when nothing about the package changed.

## Baseline Measurements

Added an integration performance probe with:

- 2000 synthetic cached plugins.
- A synthetic `.tseq` package containing 620 audio/MIDI-like files.
- Direct refresh probes for `BrowserPanelComponent` and `PluginBrowserComponent`.

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][browser][perf]"
```

Baseline before optimization:

| Probe | Total | Hot Phase |
|---|---:|---|
| `BrowserPanel refresh no-package plugins=2000` | 8.994 ms | `applyFilters` 6.819 ms, registry copy 1.554 ms |
| `BrowserPanel refresh package-files=620 plugins=2000` | 84.251 ms | `rebuildProjectFiles` 74.079 ms, `applyFilters` 8.405 ms |
| `PluginBrowser refresh plugins=2000` | 3.690 ms | `applyFilters` 1.478 ms, registry copy 2.075 ms |

Initial hot paths:

- Recursive project package scanning dominated refreshes with a package open.
- Browser Panel plugin filtering concatenated/search-prepared plugin text even when the search box was empty.
- Scan timers rebuilt plugin rows while a scan was still running, even though the registry only changes at scan completion.
- Repeated refreshes recopied unchanged plugin registry contents and rebuilt identical rows.
- `BrowserPanelComponent::selectedRowsChanged()` called `updateContent()` on selection changes.

## Changes Made

- Added opt-in phase timers for:
  - `BrowserPanelComponent::refresh`
  - `BrowserPanelComponent::refreshPluginCache`
  - `BrowserPanelComponent::applyFilters`
  - `BrowserPanelComponent::rebuildProjectFiles`
  - `BrowserPanelComponent::refreshStatus`
  - `BrowserPanelComponent::timerCallback`
  - `BrowserPanelComponent::dragPayloadForRow`
  - `PluginBrowserComponent::refresh`
  - `PluginBrowserComponent::refresh plugin-cache`
  - `PluginBrowserComponent::refresh track-selector`
  - `PluginBrowserComponent::applyFilters`
  - `PluginBrowserComponent::refreshStatus`
  - `PluginBrowserComponent::timerCallback`
- Added `tests/integration/BrowserPerformanceProbeTest.cpp` with the tag `[integration][browser][perf]`.
- Added a `PluginRegistry::revision()` counter so UI refreshes can cheaply detect unchanged registry contents.
- Browser Panel and Plugin Browser now skip registry copies when the plugin registry revision is unchanged.
- Browser Panel and Plugin Browser now skip row/filter rebuilds when the filter/search inputs and source data are unchanged.
- Browser Panel project-file scans are cached by:
  - Current project package path.
  - Project audio-source references.
  - Explicit force reload from the Browser Panel Reload button.
- Browser Panel scan timer now refreshes status only while a scan is running and rebuilds plugin rows once at scan completion.
- Overlay Plugin Browser scan timer now refreshes status only while a scan is running and does a full refresh once at scan completion.
- Browser Panel empty-search matching now returns immediately instead of building temporary search haystacks.
- Browser Panel and Plugin Browser reserve filtered vectors before rebuilding large lists.
- Browser Panel selection changes now repaint instead of calling `ListBox::updateContent()`.

Files changed:

- `src/engine/plugins/PluginRegistry.h`
- `src/engine/plugins/PluginRegistry.cpp`
- `src/ui/BrowserPanelComponent.h`
- `src/ui/BrowserPanelComponent.cpp`
- `src/ui/PluginBrowserComponent.h`
- `src/ui/PluginBrowserComponent.cpp`
- `tests/integration/BrowserPerformanceProbeTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/05_BROWSER_PANEL_PLUGIN_REGISTRY_AND_PROJECT_FILE_LISTING.md`

## Before/After Result

Final post-change probe command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][browser][perf]"
```

Final post-change timings:

| Probe | Before | After | Change |
|---|---:|---:|---:|
| `BrowserPanel refresh no-package plugins=2000` | 8.994 ms | 1.388 ms | about 85% faster |
| `BrowserPanel refresh package-files=620 plugins=2000` first scan | 84.251 ms | 84.823 ms | roughly unchanged; first scan still does real filesystem work |
| `BrowserPanel refresh package-cache-hit files=620 plugins=2000` | not measured before; would rescan package | 1.450 ms | repeated package refresh now skips scan/copy/filter work |
| `PluginBrowser refresh plugins=2000` | 3.690 ms | 0.586 ms | about 84% faster |

Important phase changes:

| Phase | Before | After |
|---|---:|---:|
| Browser Panel cached `refreshPluginCache` | 1.554-2.495 ms | 0-2 us |
| Browser Panel cached `rebuildProjectFiles` | 74.079-96.055 ms when package scanned | 7-11 us on cache hit |
| Browser Panel cached `applyFilters` | 6.819-8.405 ms | 49-54 us on unchanged rows |
| Plugin Browser cached `refresh plugin-cache` | 2.075-3.161 ms | 0 us |
| Plugin Browser cached `applyFilters` | 1.478-2.522 ms | 20 us |

Notes:

- The first refresh after opening/saving a package or pressing Reload still scans the package, which is the correct behavior.
- The main win is for broad project refreshes after nothing browser-relevant changed.
- Scan timers are now much cheaper during active scans because they update labels/status instead of rebuilding browser models.

## Hot Paths Found

1. Project package file enumeration was the largest avoidable repeat cost.
   - The recursive scan is still real work, but it no longer runs on every broad refresh.

2. Large plugin registries made refreshes copy and refilter thousands of items.
   - The registry revision lets UI components skip unchanged snapshots.
   - Row/filter dirty checks prevent identical list rebuilds.

3. Empty-search filtering was doing unnecessary string assembly.
   - Browser Panel now matches immediately when the search field is empty.

4. Scan-status timers were too heavy.
   - They now poll and display scan status while scanning, then refresh the list once when the scan completes.

## Verification Run

- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][browser][perf]"` with `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][browser][perf]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[plugin-registry]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][meter]"`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app` and verified matching binary hashes.

Result:

- Debug app target rebuilt successfully.
- Core test target built and `ctest --preset tests` passed.
- Focused integration target rebuilt successfully.
- Browser performance probe passed.
- Plugin registry tests passed.
- Focused non-VST package/import integration passed.
- Focused non-VST meter integration passed when run serially.
- Both debug app bundle paths contain the same binary hash: `0394b41096259ed871d150d874aa806795280ea97f9dfeddf76c2da53473e465`.

Known existing warnings/notes:

- JUCE assertion in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- Duplicate `src/core/libtsq_core.a` linker warning.
- JUCE no-symbol archive warnings for `juce_audio_processors_headless_ara.cpp.o` and `juce_audio_processors_headless_lv2_libs.cpp.o`.
- Existing `VstStateRegressionTest.cpp` unused-variable warning during the broad relink.
- Locale warnings from `shasum` because the shell environment uses `C.UTF-8`, which this macOS Perl build does not support.

## Remaining Risks

- First package scan can still take about 80-100 ms for hundreds of files; a background project-file indexer would be the next step if first-scan latency matters.
- The cache does not watch arbitrary external package-directory changes. Pressing Reload forces a rescan.
- Drag payload construction is timed but not directly stress-tested with thousands of drag starts.
- Real plugin scans were not run in this segment; only scan timer behavior was optimized by code inspection and local refresh path changes.
- Browser row painting was not separately profiled; this pass focused on refresh/model churn.

## Suggested Next Segment

Run Segment 06 - Track Headers, Mixer UI, Meters, Routing, And Sends.

Reason:
- Browser/model refresh churn is now much cheaper for repeated refreshes.
- Mixer rows and meters are the next likely source of ongoing UI work, especially when many tracks or realtime meter updates are visible.

## Performance Segment Handoff

- Segment: 05 - Browser Panel, Plugin Registry, And Project File Listing
- User-visible symptom investigated: lag from broad browser refreshes after project actions, plugin scans, and package-backed projects.
- Baseline measurements: package-backed Browser Panel refresh was 84.251 ms with 74.079 ms in project-file scanning; no-package 2000-plugin refresh was 8.994 ms; overlay Plugin Browser refresh was 3.690 ms.
- Hot paths found: repeated package recursion, repeated plugin registry copies, repeated row/filter rebuilds, heavy scan timer refreshes, empty-search string work.
- Changes made: phase timers, browser performance probe, plugin registry revision, registry/filter/project-file cache checks, status-only scan timers, forced Reload rescan path, selection repaint instead of content rebuild.
- Files changed: `src/engine/plugins/PluginRegistry.*`, `src/ui/BrowserPanelComponent.*`, `src/ui/PluginBrowserComponent.*`, `tests/integration/BrowserPerformanceProbeTest.cpp`, `tests/CMakeLists.txt`, `docs/performance-audit/05_BROWSER_PANEL_PLUGIN_REGISTRY_AND_PROJECT_FILE_LISTING.md`.
- Verification run: debug app build, core tests, browser performance probe, plugin-registry tests, package/meter integrations, and debug bundle sync/hash check.
- Before/after result: repeated no-package Browser Panel refresh dropped to 1.388 ms; repeated same-package refresh dropped to 1.450 ms; overlay Plugin Browser repeated refresh dropped to 0.586 ms.
- Remaining risks: first package scan remains synchronous, external package changes require Reload, real VST scan not run.
- Suggested next segment: 06 - Track Headers, Mixer UI, Meters, Routing, And Sends.
