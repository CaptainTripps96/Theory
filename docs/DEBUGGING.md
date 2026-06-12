# Debugging

## Diagnostics Log

Each app session writes a diagnostics log to:

```sh
~/Library/Application Support/TheorySequencer/diagnostics.log
```

The file is truncated on app startup so a fresh repro is easy to inspect. The Diagnostics panel also shows this path at the top.

## Performance Trace

Opt-in scoped performance timing is available in debug builds and prints to process stderr/stdout:

```sh
TSQ_PERF_TRACE=1 TSQ_PERF_THRESHOLD_US=1000 \
  out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer
```

Environment variables:

- `TSQ_PERF_TRACE=1` enables trace output.
- `TSQ_PERF_THRESHOLD_US=<microseconds>` sets the minimum timing to print.
- `TSQ_PERF_THRESHOLD_MS=<milliseconds>` is an alternate threshold form.
- If no threshold is provided, timings at or above 1000 microseconds are printed.

The performance trace is off by default. Use `docs/SYSTEM_PERFORMANCE_AUDIT_PLAN.md` and `docs/performance-audit/00_BASELINE.md` for the current audit workflow.

## VST State Traces

Focused plugin-state snapshots are opt-in because they are verbose and can affect editing performance. Enable them only when investigating plugin-state persistence:

```sh
TSQ_PLUGIN_STATE_TRACE=1 \
  out/build/debug/src/app/tsq_app_artefacts/Debug/TheorySequencer.app/Contents/MacOS/TheorySequencer
```

When enabled, the app logs focused plugin-state snapshots around high-risk events:

- Piano-roll mouse down and double click.
- Piano-roll edit commands, including note creation.
- Playback dirty marking.
- Deferred plugin-parameter restore scheduling.
- Project playback sync.
- Return-to-zero.
- Playback start.

For the current Synthesizer regression, traces include:

- `Osc1 Mod Amount`
- `Osc1 Wavefold Amount`
- `Osc1 Carrier Wave`

They also include the first few non-default hosted parameters for other plugins. If a future bug is hard to reproduce under automation, reproduce it once in the debug app, then inspect `diagnostics.log` for the first event where the watched parameter values changed unexpectedly.

## Focused VST Regression

The app-enabled integration test target is:

```sh
cmake --build --preset debug --target tsq_engine_integration_tests
ctest --test-dir out/build/debug -R tsq_engine_integration_tests --output-on-failure
```

The test uses the real installed `Synthesizer.vst3` when available. It edits the three Oscillator 1 controls, emulates piano-roll single-click and double-click note-entry behavior through JUCE mouse events, then verifies the parameters survive sync, return-to-zero, playback, and an intentional reset-to-default challenge.
