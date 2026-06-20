# Segment 15 - Synthetic Project Fixtures And Regression Benchmarks

Date: 2026-06-14

## Scope

Created reusable synthetic project fixtures and a benchmark-style integration probe that future performance work can run from a clean checkout.

This segment did not run the expensive Synthesizer/VST parameter-wipe stress harness.

## User-Visible Symptom Investigated

Performance fixes are hard to keep fixed when every segment creates its own project generator. The app now has enough focused probes that the next risk is inconsistency: a future change could improve one scenario while regressing serializer, MIDI import, command mutation, or sync behavior somewhere else.

## Baseline Measurements

Added:

- `tests/performance/SyntheticProjectFixtures.h`
- `tests/integration/SyntheticProjectBenchmarkTest.cpp`
- CMake wiring for `[integration][synthetic][perf]`

Baseline command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][synthetic][perf]"
```

## Fixture Tiers

| Scenario | Tracks | MIDI Notes | Automation Points | Audio Clips | Device Slots | JSON Size |
|---|---:|---:|---:|---:|---:|---:|
| small | 16 | 100 | 10 | 6 | 32 | 85,036 bytes |
| medium | 64 | 1,000 | 100 | 30 | 128 | 522,775 bytes |
| large | 128 | 5,000 | 1,000 | 62 | 256 | 1,975,163 bytes |
| sync-smoke | 16 | 1,000 | 0 | 0 | 0 | not serialized |

The model fixture includes MIDI tracks, audio tracks with placeholder file references, return tracks, return sends, automation lanes, and fake device chains. The Tracktion sync smoke intentionally disables fake devices/audio clips so it measures project materialization rather than missing-plugin behavior.

## Benchmark Results

| Probe | small | medium | large |
|---|---:|---:|---:|
| Build project | 0.833 ms | 6.418 ms | 29.484 ms |
| Serialize project | 29.737 ms | 167.627 ms | 547.676 ms |
| Deserialize project | 17.857 ms | 82.151 ms | 337.469 ms |
| Execute AddNote | 0.430 ms | 0.060 ms | 0.156 ms |
| MIDI export | 2.774 ms | 6.581 ms | 37.062 ms |
| MIDI import | 3.372 ms | 67.724 ms | 1,670.679 ms |

Additional sync smoke:

| Probe | Result |
|---|---:|
| Tracktion sync, 16 tracks / 1,000 MIDI notes / no fake devices | 115.697 ms |

## Changes Made

- Added a reusable synthetic project fixture helper under `tests/performance`.
- Added a benchmark-style integration test with the tag `[integration][synthetic][perf]`.
- The benchmark prints timings with `ScopedPerformanceTimer` and stable project summaries with `writePerformanceTrace`.
- The benchmark asserts model-size correctness and round-trip consistency, but it does not fail CI on timing thresholds.
- The fixture creates deterministic projects with:
  - 16, 64, and 128 tracks.
  - 100, 1,000, and 5,000 total MIDI notes.
  - 10, 100, and 1,000 total automation points.
  - Audio clip references, return tracks, return sends, and fake device chains.
  - A no-device/no-audio sync-smoke project for Tracktion materialization.

Files changed:

- `tests/performance/SyntheticProjectFixtures.h`
- `tests/integration/SyntheticProjectBenchmarkTest.cpp`
- `tests/CMakeLists.txt`
- `docs/performance-audit/15_SYNTHETIC_PROJECT_FIXTURES_AND_REGRESSION_BENCHMARKS.md`

## Hot Paths Found

1. MIDI import scales poorly at 5,000 notes.
   - The large scenario took 1.671 seconds to import a 45 KB MIDI byte stream.
   - This is the clearest new benchmark-derived optimization target.

2. Large project JSON serialization/deserialization is measurable but predictable.
   - Large serialize: 547.676 ms.
   - Large deserialize: 337.469 ms.

3. Direct AddNote command cost is low across fixture sizes.
   - Large scenario: 0.156 ms.

4. Tracktion sync smoke remains a meaningful local benchmark.
   - 16 tracks / 1,000 notes synced in 115.697 ms with fake device/audio behavior excluded.

## Verification Run

Completed verification:

- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 ./out/build/debug/tests/tsq_engine_integration_tests "[integration][synthetic][perf]"`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- Source-only whitespace check: `git diff --check -- src tests docs`
- `cmake --build --preset debug --target tsq_app`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app`.
- Verified matching executable hash:
  - `c8d99f2e4aa5304be6ab566f81a1eb167a26bcaa6ea33301654e3666d2ad39a2`

Focused synthetic benchmark result:

- 1 test case passed.
- 39 assertions passed.

Known existing warnings/notes:

- Duplicate `src/core/libtsq_core.a` linker warning.
- `shasum` emitted the known macOS locale warning before printing matching hashes.

## Remaining Risks

- There are no timing assertions by design. This avoids flaky CI failures, but regressions still require humans or future tooling to compare trace output.
- The large fixture is still a debug-build benchmark and should not be treated as release performance.
- The fixture uses placeholder audio paths and fake device references; it measures model/serialization behavior, not real file IO or real plugin load time.
- The MIDI import hot path is measured but not optimized in this segment.

## Suggested Next Segment

Run Segment 16 - Final Synthesis And Prioritized Fix Roadmap.

Reason:

- The audit now has baseline notes, focused subsystem probes, broad allocation measurements, and a reusable synthetic benchmark tier.
- The next useful step is combining the evidence into a prioritized roadmap.

## Performance Segment Handoff

- Segment: 15 - Synthetic Project Fixtures And Regression Benchmarks
- User-visible symptom investigated: performance fixes need reusable synthetic projects and repeatable benchmark commands so lag regressions are easier to catch.
- Baseline measurements: small/medium/large synthetic tiers measured build, serialize, deserialize, AddNote, MIDI export, MIDI import, and a Tracktion sync smoke.
- Hot paths found: 5,000-note MIDI import took 1.671 seconds; large JSON serialize/deserialization took 547.676 ms / 337.469 ms; direct AddNote remained low.
- Changes made: reusable synthetic project fixtures, `[integration][synthetic][perf]` benchmark probe, CMake wiring, Segment 15 report.
- Files changed: `tests/performance/SyntheticProjectFixtures.h`, `tests/integration/SyntheticProjectBenchmarkTest.cpp`, `tests/CMakeLists.txt`, this report.
- Verification run: debug integration build, focused synthetic benchmark, core tests preset, source-only diff check, debug app build/sync, matching executable hash.
- Before/after result: no optimizer before/after in this segment; new repeatable baseline captured for future comparisons.
- Remaining risks: no timing thresholds, fake audio/device behavior only, MIDI import hot path still needs a dedicated optimization segment.
- Suggested next segment: 16 - Final Synthesis And Prioritized Fix Roadmap.
