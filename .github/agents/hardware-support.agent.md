---
description: Guide maintainers through Meshtastic hardware support context generation, board intake assessment, and optional scaffold generation.
---

# Hardware Support Workflow

Use this workflow when a maintainer wants to add support for a new board variant in the Meshtastic firmware repository.

## Goals

- Reuse repository-backed hardware patterns before drafting new board files.
- Keep all generated output scoped to the requested architecture and board.
- Stop and surface evidence gaps instead of inventing pin mappings or metadata.

## Required Inputs

Collect or confirm these fields before scaffold generation:

- PlatformIO environment name
- hardware model identifier
- display name
- architecture

Recommended additional inputs:

- hardware model slug
- actively supported flag
- support level
- source materials such as schematic, pinout, or datasheet links
- board notes covering revision scope and known uncertainty

## Workflow

### 1. Refresh Repository Context

Run from the repository root:

```bash
python3 bin/generate_hardware_support_context.py
```

Review [docs/hardware-support-context.md](../../docs/hardware-support-context.md) for architecture-specific examples, metadata keys, and inherited-default notes.

### 2. Capture The Intake Request

Create or update a JSON file matching the contract in [specs/129-hardware-support-agent/contracts/board-intake-contract.md](../../specs/129-hardware-support-agent/contracts/board-intake-contract.md).

### 3. Assess Intake Readiness

Run:

```bash
python3 bin/board_intake.py path/to/intake.json
```

What to look for:

- expected artifacts
- required metadata
- matched repository patterns
- evidence gaps
- risk flags
- next actions
- scaffold readiness decision

For CI-style gating, use:

```bash
python3 bin/board_intake.py path/to/intake.json --validate
```

If the assessment is not scaffold-ready, stop and resolve the blocking gaps before continuing.

### 4. Generate Scaffold Output When Ready

Only run this when the intake assessment reports `Scaffold ready: Yes`.

```bash
python3 bin/board_scaffold.py path/to/intake.json --output-dir generated/hardware-support
```

Expected outputs:

- draft `variant.h`
- draft `platformio.ini`
- optional `variant.cpp` for ESP32-family targets

Review all `// TODO: verify — ...` annotations before treating the scaffold as merge-ready.

### 5. Compile-Gate The Target Environment (Required)

The end stage must always validate that the target environment is at least compilable.

Run:

```bash
pio run -e <environment_name>
```

Expected behavior:

- If compile succeeds, include a "compile check passed" note in the review summary.
- If compile fails, treat it as a blocking issue and report the exact failing error.
- Do not mark the workflow complete while compile is failing.

Common first-pass blocker for new scaffolds:

- Missing or placeholder `board = ...` in `platformio.ini` causes `BoardConfig: Board is not defined`.

## Guardrails

- Do not change live firmware runtime code under `src/` as part of this workflow.
- Do not modify existing board definitions under `variants/` automatically.
- Do not guess unresolved radio, display, GPS, power, or input pin mappings.
- Treat multi-revision or multi-option board notes as blocking until the revision scope is explicit.
- Check inherited BSP defaults for `nrf52840`, `rp2040`, `stm32`, and `native` targets before declaring a missing define.

## Validation Expectations

- Run `trunk fmt --force` on touched Python workflow files.
- Re-run `python3 bin/generate_hardware_support_context.py` after changes affecting context output.
- Use fixture-driven smoke tests in `bin/fixtures/` for intake and scaffold workflows.
- Run `pio run -e <environment_name>` as a required final compile gate for the generated board environment.
- Report any skipped validation or remaining TODO annotations in the review notes.
