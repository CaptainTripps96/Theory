# Segment 20 - Expression Mode Full Performance Pass

Date: 2026-06-15

## Scope

Audited the Expression Mode implementation after the playback/export prompts landed.

This pass focused on the prepared-expression hot path used by:

- first-party Simple Osc Complex modulation streams
- mixer expression playback routes
- MIDI CC export rendering
- dense sync/playback-start scenarios

## Changes Made

- Added performance trace labels for prepared expression model construction.
- Added performance trace labels for Tracktion expression playback route preparation and native-device stream installation.
- Removed duplicate prepared-expression model construction during Tracktion sync.
  - Previously, sync prepared the full expression render model once for native first-party modulation and again during `finishProjectSync` for mixer playback routes.
  - Sync now prepares once and reuses that model for native first-party routes and mixer playback route state.
- Added a dense Expression Mode integration performance probe covering:
  - 100 notes / 2 expression lanes
  - 1,000 notes / 8 expression lanes
  - phrase envelopes
  - cyclic LFO clips
  - pitch slurs
  - first-party parameter routes
  - mixer volume/pan routes
  - return-to-zero
  - playback start
  - sync after a note edit

Files changed:

- `src/core/sequencing/PreparedExpressionRenderModel.cpp`
- `src/engine/TracktionPlaybackEngine.cpp`
- `tests/integration/TracktionSyncPerformanceProbeTest.cpp`
- `docs/EXPRESSION_MODE_IMPLEMENTATION_PROMPT_PACK.md`
- this report

## Probe Command

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./build/tests/tsq_engine_integration_tests "*[expression][perf][sync]*"
```

## Trace Results

Representative debug run:

| Probe | 100 Notes / 2 Lanes | 1,000 Notes / 8 Lanes |
|---|---:|---:|
| Prepared clip render data | 3.397 ms | 19.941 ms |
| Prepared expression render model | 3.456 ms | 19.984 ms |
| Tracktion expression playback route preparation | 3.475 ms | 20.031 ms |
| Native expression route stream installation | 0.013 ms | 0.186 ms |
| Full sync | 13.484 ms | 30.237 ms |
| Return to zero | 0.025 ms | 0.234 ms |
| Playback start | 283.552 ms | 208.286 ms |
| Sync after note edit | 29.776 ms | 33.610 ms |

## Findings

1. Prepared expression construction is now visible and bounded in the trace.
   - The dense 1,000-note / 8-lane fixture prepared in about 20 ms in this debug run.
   - This is acceptable for a debug integration fixture, but it should remain a watched path as expression clips become more complex.

2. Native stream installation is cheap after the prepared model exists.
   - The dense fixture installed native route streams in under 0.2 ms.
   - Reusing the prepared model removes a clear duplicate cost from Tracktion sync.

3. Playback start is still the largest measured expression-adjacent spike.
   - The trace shows playback start at roughly 200-280 ms in this headless Tracktion debug run.
   - This appears broader than expression model preparation, because expression preparation has already completed before playback start.
   - Future work should continue the Segment 18 playback-start path rather than focusing only on expression data.

4. Sync after note edit still uses the full edit rebuild path in this fixture.
   - The note edit changes clip materialization, so Tracktion sync legitimately rebuilds the edit graph.
   - Future expression-only edits should avoid forcing note clip materialization when possible.

## Validation

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

Known existing headless notes:

- JUCE assertion logging appears in `juce_AudioPluginFormatManager.cpp:79` during Tracktion setup.
- JUCE assertion logging appears in `juce_Component.cpp:3022` during headless UI component paint probes.
- The app target has no safe one-shot CLI smoke mode; the integration probes exercise app services, UI components, and Tracktion startup instead.

## Remaining Risks

- Expression-only command dirty categories still need a dedicated UI/service pass so expression edits do not imply unnecessary clip materialization.
- Prepared expression data is rebuilt at project sync scope. A future cache should reuse per-clip/lane prepared data by fingerprint when only unrelated clips or tracks change.
- Playback start remains noticeably expensive in debug probes and should stay on the performance roadmap.
- MIDI CC export now uses prepared expression routes, so very dense export jobs should get a later export-specific performance budget once export UI is wired to the project-aware path.
