# IKOKA STICK 0.3.0

Meshtastic firmware variant for the IKOKA STICK 0.3.0 with an EBYTE E22-400M33S SX1268 LoRa module.

## Builds

Normal Meshtastic firmware:

```bash
pio run -e ikoka_stick_0_3_0
```

Dedicated RF lab firmware:

```bash
pio run -e ikoka_stick_0_3_0_rf_test
```

See `RF_TEST_OPERATOR.md` for RF-test operation and safety notes.

See `RF_TEST_LLM_NOTES.md` for implementation context.
