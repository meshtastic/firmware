# Data Model: Hardware Support Agent

## Hardware Support Context

Purpose: Repository-backed summary of current target-definition patterns across supported board variants.

Fields:

- source_paths: list of repository glob roots used to build the context
- generated_at: timestamp or generation marker
- variant_count: integer count of scanned variant directories
- environment_count: integer count of summarized PlatformIO environments
- metadata*keys: list of observed `custom_meshtastic*\*` keys
- category_counts: mapping of capability category to common macros and frequencies
- architecture_inventory: list of architecture groups and their environments
- representative_examples: list of example boards with extracted metadata and macro samples
- cautions: list of caveats about inherited defaults, multi-environment boards, and verification limits

Validation rules:

- Must be derived from repository state rather than hand-maintained guesses.
- Must clearly separate explicit declarations from inherited/default behavior where known.
- Must remain read-only context and not imply that any new board is validated for merge.

Relationships:

- Used by Board Intake Request as the canonical repository pattern source.

## Board Intake Request

Purpose: Maintainer-supplied description of a proposed new board or board revision.

Fields:

- environment_name: proposed PlatformIO environment name
- hardware_model: numeric or repository-convention hardware model identifier
- hardware_model_slug: uppercase slug when known
- display_name: human-readable board name
- architecture: target family such as `esp32-s3` or `nrf52840`
- source_materials: optional list of schematic, pinout, datasheet, or board-page references
- support_level: optional intended support metadata
- actively_supported: optional boolean intent
- board_notes: optional maintainer notes about revisions, optional peripherals, or known gaps

Validation rules:

- `environment_name`, `hardware_model`, and `display_name` are required for phase 1 intake.
- Architecture is required before any artifact expectation can be considered complete.
- Source materials are optional for submission but required for moving unresolved hardware fields toward scaffold generation.
- Conflicts with existing environment names or hardware identifiers must be surfaced.

Relationships:

- Produces one Intake Assessment.
- May eventually lead to one or more Board Support Scaffolds.

## Intake Assessment

Purpose: Structured result of evaluating a Board Intake Request against repository patterns and evidence sufficiency.

Fields:

- expected_artifacts: list of files or sections likely needed, such as `variant.h`, optional `variant.cpp`, PlatformIO environment entries, board metadata, images, or tags
- required*metadata: list of mandatory `custom_meshtastic*\*` values and board-definition fields
- matched_patterns: list of related repository examples by architecture or board family
- evidence_gaps: list of unresolved or conflicting hardware facts
- risk_flags: list of issues such as ambiguous board revisions, unsupported peripherals, or inherited-default uncertainty
- next_actions: ordered maintainer actions needed before safe scaffold generation
- scaffold_ready: boolean indicating whether evidence is sufficient for a later scaffold phase

Validation rules:

- Must never infer unsupported pin mappings silently.
- Must describe missing information in maintainer-actionable language.
- Must remain architecture- and variant-scoped.

Relationships:

- Derived from Board Intake Request and Hardware Support Context.
- Blocks or permits creation of Board Support Scaffold.

## Evidence Gap

Purpose: Specific missing, conflicting, or ambiguous fact that prevents safe draft generation.

Fields:

- category: metadata, radio, display, input, GPS, power, storage, connectivity, or revision-scope
- description: maintainer-readable explanation of what is missing or conflicting
- affected_artifact: target file or configuration area impacted
- required_evidence: type of source needed to resolve the gap
- blocking: boolean indicating whether the gap prevents scaffold generation

Validation rules:

- Must be traceable to a missing repository pattern or missing board truth.
- Must not be collapsed into generic “needs more info” language when the specific blocker is knowable.

Relationships:

- Belongs to an Intake Assessment.

## Board Support Scaffold

Purpose: Draft board-support content for a new target once evidence is sufficient.

Fields:

- target_variant_dir: proposed variant directory path
- variant_h_content: draft content or structured sections for `variant.h`
- variant_cpp_content: optional draft for `variant.cpp`
- platformio_env_content: draft PlatformIO environment metadata and extends chain
- unresolved_annotations: inline markers for any remaining non-blocking maintainer review items
- source_basis: references to intake data and repository patterns used to draft content

Validation rules:

- Must only include fields backed by evidence and repository conventions.
- Must preserve variant-scoped truth and never modify unrelated board definitions.
- May only be produced when Intake Assessment marks `scaffold_ready` true.

Relationships:

- Produced from Intake Assessment after gaps are resolved.
