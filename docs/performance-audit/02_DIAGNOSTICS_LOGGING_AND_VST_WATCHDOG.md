# Segment 02 - Diagnostics, Logging, And VST Watchdog Verbosity

Date: 2026-06-10

## Scope

Measured whether VST/plugin-state diagnostics are adding avoidable lag to ordinary project mutations. The measurement used the focused non-VST package/import integration path as a lightweight proxy for edit-like command-stack mutations.

The expensive Synthesizer/VST parameter-wipe stress harness was not run.

## User-Visible Symptom Investigated

The app feels broadly laggy. Segment 00 showed shared UI pressure, and this segment checked whether recent plugin-state debugging added extra command/edit latency by writing verbose diagnostics during normal operations.

## Baseline Measurements

Command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]" \
  > /tmp/tsq_perf_segment02_before.log 2>&1
```

Before gating verbose plugin-state traces:

| Label | Samples | Avg | Max |
|---|---:|---:|---:|
| `AppServices startup body` | 1 | 2107.819 ms | 2107.819 ms |
| `Logger::log` | 38 | 0.271 ms | 5.071 ms |
| `AppServices::markPlaybackProjectDirty` | 2 | 3.055 ms | 3.345 ms |
| `AppServices::restoreObservedPluginParameterStateSoon` | 2 | 1.418 ms | 1.553 ms |
| `AppServices::tracePluginState` | 8 | 0.668 ms | 0.872 ms |

Log volume:
- Process output: 107 lines.
- Diagnostics log: 38 lines.
- Plugin-state/trace lines: 24 in process output and 24 in diagnostics log.

Interpretation:
- A lightweight import path with two dirty project mutations wrote 24 plugin-state trace lines.
- Each trace line used `Logger::log`, which stores the entry, writes to console, and appends to the diagnostics file.
- `markPlaybackProjectDirty()` averaged about 3 ms in this path largely because it nested multiple plugin-state trace calls.

## Changes Made

Changed verbose plugin-state tracing from always-on to opt-in:

- `AppServices::tracePluginState()` now returns immediately unless `TSQ_PLUGIN_STATE_TRACE=1` is set.
- The actual VST parameter observation/restoration watchdog remains active.
- `docs/DEBUGGING.md` documents how to re-enable plugin-state traces for VST state investigations.
- `docs/KNOWN_ISSUES.md` now reflects that focused plugin-state traces are opt-in.

## Before/After Result

After gating verbose plugin-state traces:

Command:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]" \
  > /tmp/tsq_perf_segment02_after.log 2>&1
```

| Label | Samples | Avg | Max |
|---|---:|---:|---:|
| `AppServices startup body` | 1 | 1943.494 ms | 1943.494 ms |
| `Logger::log` | 14 | 0.423 ms | 4.058 ms |
| `AppServices::markPlaybackProjectDirty` | 2 | 0.086 ms | 0.090 ms |
| `AppServices::restoreObservedPluginParameterStateSoon` | 2 | 0.013 ms | 0.023 ms |

Log volume:
- Process output: 51 lines.
- Diagnostics log: 14 lines.
- Plugin-state/trace lines: 0.

Delta:

| Measurement | Before | After | Change |
|---|---:|---:|---:|
| Diagnostics log lines | 38 | 14 | -63% |
| Plugin-state/trace lines | 24 | 0 | -100% |
| `Logger::log` calls | 38 | 14 | -63% |
| `markPlaybackProjectDirty` avg | 3.055 ms | 0.086 ms | about 35x faster |
| `restoreObservedPluginParameterStateSoon` avg | 1.418 ms | 0.013 ms | about 109x faster in this no-plugin path |

## Opt-In Trace Verification

Command:

```sh
TSQ_PLUGIN_STATE_TRACE=1 TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=0 \
  ./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]" \
  > /tmp/tsq_perf_segment02_optin.log 2>&1
```

Result:
- Plugin-state/trace lines returned: 24 in process output and 24 in diagnostics log.
- `AppServices::tracePluginState` appeared in performance output again.
- This confirms the diagnostic path remains available when explicitly requested.

## Hot Paths Found

1. Always-on plugin-state tracing was a real lag contributor for ordinary mutations.
   - The tested import path is not a plugin-heavy scenario, yet trace logging still materially affected dirty-mark timing.

2. `Logger::log` is expensive enough to keep out of high-frequency paths.
   - It opens/appends the diagnostics file per log entry and writes to console when enabled.
   - A later logging segment could batch file output or keep console logging off by default, but the immediate win was to stop emitting unnecessary lines.

3. The VST watchdog still deserves separate measurement with a real loaded plugin.
   - This segment kept `observeLivePluginParameterState()` and `restoreObservedPluginParameterStateSoon()` active.
   - With no plugin loaded, those paths are now cheap after trace logging is disabled.

## Files Changed

- `src/app/AppServices.cpp`
- `docs/DEBUGGING.md`
- `docs/KNOWN_ISSUES.md`
- `docs/performance-audit/02_DIAGNOSTICS_LOGGING_AND_VST_WATCHDOG.md`

## Verification Run

- `cmake --build --preset debug --target tsq_engine_integration_tests`
- `./out/build/debug/tests/tsq_engine_integration_tests "[integration][package][import]"`
- Opt-in trace verification with `TSQ_PLUGIN_STATE_TRACE=1`
- `cmake --build --preset debug --target tsq_app`
- `cmake --build --preset tests --target tsq_core_tests`
- `ctest --preset tests --output-on-failure`
- Synced `out/build/debug/.../TheorySequencer.app` to `build/.../Debug/TheorySequencer.app` and verified matching binary hashes.

Result:
- Focused non-VST integration passed.
- Debug app target rebuilt successfully.
- Core tests passed.
- Both debug app bundle paths now contain the same binary.

## Remaining Risks

- This segment measured command-stack mutation logging, not a full manual piano-roll double-click note-entry session.
- The real VST watchdog can still be expensive when an actual plugin is loaded and parameter snapshots are large.
- `Logger::log` still appends to disk per entry; heavy non-plugin logging could still become a bottleneck.
- Segment 00's recurring Timeline paint/Main timer pressure remains unresolved.

## Suggested Next Segment

Run Segment 01 - App Shell, Main Message Loop, And Global Timers.

Reason:
- Segment 00 showed the 24 Hz main timer unconditionally triggering Timeline repaint and Track List / Detail Editor refresh.
- Segment 02 removed avoidable diagnostics noise, so the next biggest shared lag source is the global UI loop.
