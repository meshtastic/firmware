# BME680 IAQ replay harness

`bin/bme680_iaq_replay.cpp` replays a captured sensor trace through the in-tree
`BME680IaqEstimator` on a dev machine, for tuning the estimator's constants
against recorded Bosch BSEC output. The estimator is pure math with no platform
dependencies, so a trace replays in milliseconds - edit the constants in
`src/modules/Telemetry/Sensor/BME680IaqEstimator.h`, recompile, rerun.

## Build

From the repo root:

```bash
c++ -std=c++17 -O2 -I src -o /tmp/iaq_replay \
    bin/bme680_iaq_replay.cpp src/modules/Telemetry/Sensor/BME680IaqEstimator.cpp
```

## Input

CSV on stdin or as a file argument, one sample per line:

```text
gas_ohms,relative_humidity[,bsec_iaq]
```

Lines starting with `#` are ignored; a single non-numeric header row is
tolerated; any other malformed line is reported on stderr and skipped.

## Capturing a trace

On a firmware build that still links BSEC (any release tag before the BSEC
removal), add one log line to `BME680Sensor::getMetrics` in the BSEC branch:

```cpp
LOG_INFO("IAQCSV,%.0f,%.2f,%.0f", bme680.getData(BSEC_OUTPUT_RAW_GAS).signal,
         bme680.getData(BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY).signal,
         bme680.getData(BSEC_OUTPUT_IAQ).signal);
```

then extract the columns from the serial log:

```bash
grep -o 'IAQCSV,.*' serial.log | cut -d, -f2- > trace.csv
```

BSEC's `RAW_GAS` and heat-compensated humidity are exactly the estimator's
inputs, so one physical sensor feeds both algorithms identically.

## Output

Per-sample CSV `n,gas_ohms,rh,est_iaq,bsec_iaq` on stdout (empty `est_iaq`
during the estimator's warm-up/burn-in window), plus a stderr summary with the
mean absolute error and UI-band agreement against the `bsec_iaq` column, using
the same 0-500 band thresholds the device screen applies.
