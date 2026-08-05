# Meshtastic MCP Server — Test Harness

Automated test suite for the MCP server, organized around real operator
concerns rather than generic "unit vs hardware".

## Tiers

| Dir             | Hardware                | Question this tier answers                                            |
| --------------- | ----------------------- | --------------------------------------------------------------------- |
| `unit/`         | none                    | Do the parsing / filtering / profile-generation primitives work?      |
| `provisioning/` | 1 device, per-test bake | Did my pre-bake recipe stick? Does it survive a factory reset?        |
| `admin/`        | 1 device, shared bake   | Do my daily admin ops (owner, channel URL, config writes) round-trip? |
| `mesh/`         | 2 devices, shared bake  | Do my devices actually form a mesh? Send + receive? ACKs?             |
| `telemetry/`    | 2 devices, shared bake  | Is telemetry reporting? Is position broadcast correct?                |
| `monitor/`      | 1 device, shared bake   | Is the boot log clean (no panics)?                                    |
| `fleet/`        | varies                  | Are my CI runs isolated from each other? Are reflashes idempotent?    |

## Quick start

```bash
cd mcp-server
pip install -e ".[test]"

# No hardware — 33 unit tests, ~3 seconds
pytest tests/unit -v

# Hub attached (nRF52840 + ESP32-S3) — first run bakes, then exercises everything
pytest tests/ --html=report.html

# Hub already baked with session profile (dev loop) — skip bake
pytest tests/ --assume-baked --html=report.html

# Force a rebake (new firmware, new seed, etc.)
pytest tests/ --force-bake --html=report.html
```

## CLI flags

- `--force-bake` — always reflash both roles at session start, even if the
  current state matches the session profile.
- `--assume-baked` — skip `test_00_bake.py` entirely. Use when you know the
  devices are already baked and want a fast dev loop.
- `--hub-profile=<yaml>` — point at a YAML file for non-default hub hardware.
  Default targets VID `0x239a` (nRF52) and `0x303a`/`0x10c4` (ESP32-S3).
- `--no-teardown-rebake` — skip the session-end rebake that `provisioning/`
  and `fleet/` tests perform. Useful in rapid iteration.

## Environment variables

- `MESHTASTIC_FIRMWARE_ROOT` — firmware repo path (defaults to `../` from tests/)
- `MESHTASTIC_MCP_ENV_NRF52` — PlatformIO env for the nRF52 role (default
  `rak4631`)
- `MESHTASTIC_MCP_ENV_ESP32S3` — PlatformIO env for the ESP32-S3 role (default
  `heltec-v3`)
- `MESHTASTIC_MCP_SEED` — override the session PSK seed (default:
  `pytest-<unix-ts>`). Set this to reproduce a specific failing run.

## Fixtures you'll use when adding tests

All defined in `conftest.py`:

- **`hub_devices`** → `{"nrf52": "/dev/cu.X", "esp32s3": "/dev/cu.Y"}`. Auto-
  skips the test if a required role isn't present.
- **`test_profile`** → USERPREFS dict for the session (`build_testing_profile`).
- **`no_region_profile`** → variant without `USERPREFS_CONFIG_LORA_REGION`.
- **`baked_mesh`** → verifies both devices are baked with the session profile
  (does NOT reflash — that's `test_00_bake.py`'s job).
- **`baked_single`** → single verified baked device; parametrize `request.param`
  to pick role.
- **`serial_capture`** → factory; `cap = serial_capture("esp32s3")` starts a
  pio device monitor session, drains into a per-test buffer, attaches the
  buffer to the pytest-html report on failure.
- **`wait_until`** → exponential-backoff polling helper; `wait_until(lambda:
predicate(), timeout=60)` replaces flaky `time.sleep()` patterns.

## Reports

`pytest --html=report.html` produces a self-contained HTML with:

- Per-test pass/fail/skip with timings
- On failure: serial log capture from any `serial_capture` fixture used
- On failure: `device_info` + lora config JSON for every role on the hub
- Session seed and session start time (for reproducibility)

`pytest --junitxml=junit.xml` produces CI-integration XML.

`tool_coverage.json` is emitted at session end in the tests directory — shows
which of the 38 MCP tools the run exercised. Useful for closing test gaps.

## Adding a new test

1. Pick the category that matches the operator concern (not the technical
   surface). "Does my fleet's owner name persist" is `admin/`, not `unit/`.
2. If you need both devices, depend on `baked_mesh`. If you need one, depend
   on `baked_single`. If you need to mutate hardware state, put it in
   `provisioning/` or `fleet/` and add a `try/finally` teardown that re-bakes
   the session profile.
3. Use `wait_until` for anything involving LoRa timing — fixed `sleep()`
   produces flakes.
4. Use `serial_capture` when you need to observe firmware log output (e.g.
   "did the packet get decoded?").
5. Add a `@pytest.mark.timeout(N)` — mesh tests routinely hit LoRa-airtime
   waits; default pytest timeout is infinite.

## Troubleshooting

- **All hardware tests SKIP** → hub not detected. Plug in the USB hub, verify
  with `pytest tests/ --collect-only` or `python -c "from meshtastic_mcp import
devices; print(devices.list_devices())"`.
- **`baked_mesh` fails with "devices not baked"** → run `pytest
tests/test_00_bake.py` first, or pass `--force-bake` on the full run.
- **Mesh formation tests time out** → check that both devices are on the same
  session profile (`--force-bake` forces both to the current seed).
- **Provisioning tests leave device in bad state** → teardowns re-bake, but
  if a test crashes between "bake broken state" and "bake good state", run
  `pytest tests/test_00_bake.py --force-bake` to recover.
