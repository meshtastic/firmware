# RAK4631 Voltaic V25

This variant is for a RAK4631 with a RAK5811 measuring a Voltaic V25 USB power bank on A8.

The Voltaic pack powers the node through USB, so the stock RAK4631 NRF USB/VBUS detector reports external power. This variant intentionally overrides that status and reports the Voltaic pack as the node battery so Meshtastic telemetry publishes `device_metrics.battery_level` as `0..100` instead of the USB/mains sentinel value `101`.

Build with:

```powershell
pio run -e rak4631_voltaic_v25
```

Key build flags:

- `VOLTAIC_V25_RAK5811_BATTERY=1`: enables the RAK5811 analog battery path.
- `VOLTAIC_V25_RAK5811_POWER_PIN=17`: drives the RAK5811 sensor rail.
- `VOLTAIC_V25_RAK5811_BATTERY_PIN=31`: reads A8.
- `VOLTAIC_V25_PACK_DIVIDER_RATIO=0.5`: corrects the Voltaic V25 half-voltage divider.
- `VOLTAIC_V25_RAK5811_DIVIDER_RATIO=0.6`: corrects the RAK5811 input divider.
- `VOLTAIC_V25_FORCE_BATTERY_POWER=1`: forces Meshtastic power status to battery present, USB off, not charging.

Voltage reporting:

The reported voltage is the estimated Voltaic cell voltage from ADC voltage divided by both hardware dividers:

```text
cell_mV = adc_mV / (VOLTAIC_V25_PACK_DIVIDER_RATIO * VOLTAIC_V25_RAK5811_DIVIDER_RATIO)
```

Percentage reporting:

`device_metrics.battery_level` uses piecewise linear interpolation across the manufacturer Voltaic V25 cell-voltage table:

- `2960mV`: `0%`
- `3296mV`: `25%`
- `3498mV`: `50%`
- `3699mV`: `75%`
- `3883mV`: `100%`

For future Meshtastic rebases, keep the generic analog/Voltaic support in `src/Power.cpp` and keep this variant limited to configuration, pin choices, and calibration constants.
