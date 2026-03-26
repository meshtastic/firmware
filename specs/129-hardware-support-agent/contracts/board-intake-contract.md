# Contract: Board Intake Request And Assessment

## Purpose

This contract defines the minimum maintainer input and the expected workflow output for the hardware-support-agent feature.

## Input Contract

### Required Fields

- `environment_name`: Proposed PlatformIO environment name for the new board.
- `hardware_model`: Meshtastic hardware model identifier to assign.
- `display_name`: Human-readable board name.
- `architecture`: Target architecture family such as `esp32`, `esp32-s3`, `nrf52840`, `rp2040`, or `stm32`.

### Recommended Fields

- `hardware_model_slug`: Repository-style uppercase slug if already known.
- `actively_supported`: Whether the board is intended to be actively supported.
- `support_level`: Intended `custom_meshtastic_support_level` value.
- `source_materials`: Links, file paths, or notes for schematics, pinouts, datasheets, or vendor pages.
- `board_notes`: Freeform notes about revisions, peripherals, or known uncertainty.

## Validation Rules

- The workflow must reject or pause on missing required fields.
- The workflow must detect conflicts with existing environment names or existing hardware identifiers where discoverable.
- The workflow must not invent radio, power, display, GPS, input, or auxiliary pin mappings.
- The workflow must identify when architecture defaults may be relevant and flag them for human review.

## Output Contract

### Required Assessment Sections

- `expected_artifacts`: What board-support artifacts are likely required.
- `required_metadata`: What `custom_meshtastic_*` metadata and related fields must be decided.
- `matched_patterns`: Existing repository examples that are closest to the request.
- `evidence_gaps`: Missing or conflicting facts that block safe scaffolding.
- `risk_flags`: Conditions that require special maintainer attention.
- `next_actions`: Concrete steps to move the request toward scaffold readiness.
- `scaffold_ready`: Boolean decision indicating whether draft file generation is safe.

## Artifact Expectations

Depending on the board and architecture, the assessment should consider whether the request will need:

- a new or reused variant directory under `variants/<architecture>/...`
- `variant.h`
- optional `variant.cpp`
- one or more PlatformIO environments with `custom_meshtastic_*` metadata
- board images, tags, partition-scheme metadata, or DFU metadata
- any architecture-specific board files or notes for inherited defaults

## Non-Goals For Phase 1

- Direct parsing of PDF schematics.
- Automatic merging of new board files into the repository.
- Any change to firmware runtime behavior, protocol behavior, or shared board defaults.
