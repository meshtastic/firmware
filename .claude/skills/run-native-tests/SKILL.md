---
name: run-native-tests
description: Run the native C++ unit tests with bin/run-tests.sh and read the verdict correctly, or compile-check a firmware env with pio run. Use before claiming any test or build result in this repo.
---

# Running native tests and build checks

The canonical reference is [`.github/copilot-instructions.md` → Native unit tests](../../../.github/copilot-instructions.md#native-unit-tests-c). This skill is the operating procedure; it does not restate the verdict table.

## Tests: `bin/run-tests.sh`

```bash
./bin/run-tests.sh -e native -f test_radio --quiet   # one suite, iterate
./bin/run-tests.sh --quiet                           # all suites, the gate
```

- **Run it in the background** (`run_in_background`), even for a one-line re-run. A cold build takes minutes before the first test case.
- **The verdict is the final `RESULT:` line and nothing else.** pio prints `[PASSED]` and `N succeeded` long before the verdict exists. `--quiet` prints only the `RESULT:` line.
- **One run at a time.** A second invocation prints `RESULT: BUSY` (exit 4) and touches nothing. Use `--status` to see whether a run is in progress and what the last one said, `--wait` to block until the current one ends, and `--abort` to stop it. Never `pgrep`/`pkill` for a run: the pattern matches your own shell.
- **The last verdict is on disk** in `.pio/runtests/last-result.tsv`. `--status` marks it `STALE` once the tree has changed, so a verdict from before an edit is never mistaken for a current one.
- **`-f` is not a gate.** `FILTERED` (exit 3) means the filtered suite passed. A full run can still fail on shared state that filtering removed. Gate on a full run.
- **Env.** The default `coverage` env adds gcov + ASan and is about 2× slower. `-e native` has no sanitizers, so do not claim ASan coverage from a `-e native` run.
- **`PIO_UNIT_TESTING` changes behaviour.** Some features are forced on under it, so a green suite does not cover the shipping default. When a change touches a flag-gated path, also compile the shipping env (below).

## Firmware builds: `pio run -e <env>`

The wrapper does not cover these, so read them yourself:

- **One `pio` job at a time.** All envs share `.pio/build/`. Two concurrent jobs wipe each other's objects. Queue builds, never parallelise them.
- The result is the `[SUCCESS]` / `[FAILED]` line for the env, plus any `error:` above it. The exit code of a backgrounded wrapper shell says nothing about the build.
- **nRF52:** `extra_scripts/nrf52_warm_region.py` prints `nrf52_warm_region: guard OK -- image ends at 0x…, N KB clear` on every build, or fails the build. Quote that address for flash headroom. The `Flash: xx%` line uses a different denominator and reads optimistic.
- **Which env compiles what.** A path behind a board macro (for example `LORA_DIO1_SOFTWARE_POLL`, meshnology_w10 only) compiles in no other env, and CI may not build that board. Find the env that defines it and build that one.

## Before reporting

State the exact verdict line, the commit it ran on, and what was not run (filtered suites, envs not built). A result you did not read from `RESULT:`, `[SUCCESS]` or the guard line is not a result.
