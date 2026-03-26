# Implementation Plan: Hardware Support Agent

**Branch**: `[129-hardware-support-agent]` | **Date**: 2026-03-25 | **Spec**: /Users/benmeadors/Documents/GitHub/firmware/specs/129-hardware-support-agent/spec.md
**Input**: Feature specification from `/Users/benmeadors/Documents/GitHub/firmware/specs/129-hardware-support-agent/spec.md`

**Note**: This plan covers Phase 0 and Phase 1 outputs for a constitution-safe first increment. The first delivery scope centers on repository-backed hardware context plus intake validation and report generation; scaffold generation remains designed but gated behind sufficient evidence.

## Summary

Build a Copilot-oriented hardware support workflow for Meshtastic that starts from repository-derived board-definition context, accepts a constrained board intake request, and produces a structured readiness report before any new board files are drafted. The technical approach uses lightweight Python tooling and repository-local markdown/JSON contract artifacts to inventory existing `variant.h` and `platformio.ini` patterns, normalize maintainer inputs, identify evidence gaps, and prepare a later scaffolding phase without changing live firmware behavior.

## Technical Context

**Language/Version**: Python 3.x for workflow tooling, Markdown for generated artifacts, existing C/C++/PlatformIO repository conventions for downstream scaffold targets  
**Primary Dependencies**: Python standard library for inventory/intake tooling, existing Spec Kit artifacts, repository-local Copilot prompt/agent files, PlatformIO environment metadata conventions already in `variants/**/platformio.ini`  
**Storage**: Repository-local files under `docs/`, `specs/129-hardware-support-agent/`, optional future intake examples under repo docs or fixtures  
**Testing**: Targeted script execution, generated artifact review, `python3 bin/generate_hardware_support_context.py`, `trunk fmt` where applicable for markdown/templates, and targeted follow-up validation against representative variant files  
**Target Platform**: Maintainer workflow inside the Meshtastic firmware repository on macOS/Linux development environments; outputs describe supported firmware architectures including ESP32, ESP32-S3, ESP32-C3, ESP32-C6, nRF52, RP2040/RP2350, STM32, and native patterns  
**Project Type**: Repository tooling and Copilot workflow support  
**Performance Goals**: Generate context and intake reports quickly enough for interactive maintainer use on a local checkout; avoid repository-wide processing that would materially slow a normal Copilot session  
**Constraints**: No changes to mesh protocol behavior or shared device defaults; no guessing of pin mappings; architecture-specific truth must stay variant-scoped; keep implementation dependency-light and reviewable  
**Scale/Scope**: Inventory currently spans 166 variant directories and 212 PlatformIO environments; phase 1 scope covers context generation, intake contract, evidence-gap reporting, and custom-agent planning, not autonomous end-to-end board enablement

## Constitution Check

_GATE: Must pass before Phase 0 research. Re-check after Phase 1 design._

- Safety-critical mesh impact: Pass. This feature is workflow/documentation/tooling only and does not modify routing, airtime, MQTT, channel, packet-path, or public default behavior.
- Variant and platform scope: Pass. The workflow is explicitly scoped to board-definition artifacts and requires variant-specific evidence before any scaffold output is permitted.
- Validation evidence: Pass with explicit plan. Validation for this phase is `python3 bin/generate_hardware_support_context.py`, manual spot-check against representative variants, and review of generated documentation/contracts. Later implementation phases should add targeted intake fixture checks.
- Resource, power, memory, dependency impact: Pass. The feature adds lightweight local tooling and markdown contracts only, with no runtime firmware impact and no new external runtime dependency requirement.
- Constitutional violations or gaps: No active violations for Phase 0/1 planning. The only open product choice is whether future phase 2 includes scaffold generation immediately or remains report-only until more intake validation exists.

### Post-Design Re-Check

- Safety-critical mesh impact remains unchanged after design: no firmware runtime path is altered.
- Variant-scoped hardware truth is reinforced by the intake contract and evidence-gap model.
- Validation evidence remains sufficient for design artifacts, with implementation tasks needing targeted script-level checks.
- Resource and dependency impact remains minimal and repository-local.
- No justification entries are required in Complexity Tracking for this plan.

## Project Structure

### Documentation (this feature)

```text
specs/129-hardware-support-agent/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── board-intake-contract.md
└── tasks.md
```

### Source Code (repository root)

```text
.github/
├── agents/
└── prompts/

bin/
├── generate_hardware_support_context.py
├── board_intake.py
└── board_scaffold.py

docs/
└── hardware-support-context.md

variants/
├── esp32/
├── esp32c3/
├── esp32c6/
├── esp32s2/
├── esp32s3/
├── native/
├── nrf52840/
├── rp2040/
├── rp2350/
└── stm32/
```

**Structure Decision**: Use the existing single-repository tooling structure. Planning artifacts live under `specs/129-hardware-support-agent/`, reusable generated context stays in `docs/`, implementation scripts live in `bin/`, and any future custom Copilot workflow files belong in `.github/prompts/` and `.github/agents/`. No new top-level application structure is needed.

## Complexity Tracking

No constitutional violations or complexity exceptions are currently required.

## Review Notes

- **SC-004** is a post-launch business metric and is not a blocking acceptance gate before merge. A timed baseline comparison should be collected after the first real board intake using the completed workflow.
- Validation completed so far: `python3 bin/generate_hardware_support_context.py --validate`, `python3 bin/board_intake.py bin/fixtures/intake_minimal.json --validate`, `python3 bin/board_intake.py bin/fixtures/intake_full.json`, `python3 bin/board_intake.py bin/fixtures/intake_multi_env.json`, `python3 bin/board_scaffold.py bin/fixtures/intake_full.json`, and `python3 bin/board_scaffold.py bin/fixtures/intake_multi_env.json`.
- `bin/board_scaffold.py` intentionally emits placeholder pin and metadata values plus `// TODO: verify — ...` annotations. Generated scaffold output is draft-only and not suitable for direct merge without maintainer review against schematics and existing board patterns.
- Scaffold generation currently writes to `generated/hardware-support/` rather than directly into `variants/` to keep the workflow reviewable and constitution-safe.
- Skipped validations: no targeted `pio run`, native test, or simulator run was executed because this feature adds repository tooling and generated draft artifacts only; no firmware runtime files under `src/` were changed.
