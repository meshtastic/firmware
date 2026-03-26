# Quickstart: Hardware Support Agent

## Goal

Use the repository-backed hardware support workflow to understand current board-definition patterns and evaluate whether a new board request is ready for safe scaffolding.

## Prerequisites

- Work from the repository root.
- Ensure Python 3 is available.
- Have the new board’s minimum intake information ready:
  - proposed PlatformIO environment name
  - hardware model identifier
  - display name
  - target architecture
  - any available schematic, pinout, or datasheet references

## Step 1: Regenerate the repository context artifact

Run:

```bash
python3 bin/generate_hardware_support_context.py
```

Review:

- `docs/hardware-support-context.md`

Confirm that the architectures, representative examples, and metadata keys still reflect the current repository state.

## Step 2: Prepare the board intake request

Capture the request using the contract in:

- `specs/129-hardware-support-agent/contracts/board-intake-contract.md`

At minimum, fill:

- environment name
- hardware model
- display name
- architecture

Add source materials for radio, display, power, GPS, input, and auxiliary peripherals when available.

## Step 3: Evaluate readiness

Run:

```bash
python3 bin/board_intake.py path/to/intake.json
```

The intake report now includes:

- request summary
- expected artifacts
- required metadata
- matched repository patterns
- blocking and non-blocking evidence gaps
- risk flags
- next actions
- a `scaffold_ready` decision

For gate-style validation, run:

```bash
python3 bin/board_intake.py path/to/intake.json --validate
```

This exits non-zero when blocking gaps remain.

## Step 4: Generate scaffold output when ready

If Step 3 reports `scaffold_ready: true`, run:

```bash
python3 bin/board_scaffold.py path/to/intake.json --output-dir generated/hardware-support
```

Expected outputs:

- `generated/hardware-support/variants/<arch>/<variant-dir>/variant.h`
- `generated/hardware-support/variants/<arch>/<variant-dir>/platformio.ini`
- optional `variant.cpp` for ESP32-family targets

If the intake is not scaffold-ready, the scaffold command prints the assessment and exits non-zero instead of generating files.

## Step 5: Validate the workflow artifacts

Run:

```bash
python3 bin/generate_hardware_support_context.py
python3 bin/board_intake.py bin/fixtures/intake_full.json
python3 bin/board_scaffold.py bin/fixtures/intake_full.json --output-dir generated/hardware-support
```

Then manually spot-check representative targets such as:

- `variants/esp32/tbeam`
- `variants/esp32s3/tlora-pager`
- `variants/nrf52840/t-echo`
- `variants/rp2040/rak11310`

Ensure the generated context, intake assessment, and scaffold output remain consistent with repository truth.

## Next Step

Use [hardware-support.prompt.md](.github/prompts/hardware-support.prompt.md) and [hardware-support.agent.md](.github/agents/hardware-support.agent.md) to invoke the workflow directly from Copilot.
