# Feature Specification: Hardware Support Agent

**Feature Branch**: `[129-hardware-support-agent]`  
**Created**: 2026-03-25  
**Status**: Draft  
**Input**: User description: "Implement the feature specification based on the updated constitution. I want to build an agent inside copilot to add new hardware support, including variant.h/cpp, any platformio ini environments will all of our custom metadata, and all of the pinmappings. I would start this process by just giving the board environment name, hw_model, display name, and perhaps some source materials illustrating the pin mappings in a PDF schematic for instance. I think we should start by creating a context for you in the form of a markdown file documenting all of the current input, button, radio, gpio, and other common pins we use in the meshtastic firmware at a device target definition level, so that we reuse instead of re-invent."

## User Scenarios & Testing _(mandatory)_

<!--
  IMPORTANT: User stories should be PRIORITIZED as user journeys ordered by importance.
  Each user story/journey must be INDEPENDENTLY TESTABLE - meaning if you implement just ONE of them,
  you should still have a viable MVP (Minimum Viable Product) that delivers value.

  Assign priorities (P1, P2, P3, etc.) to each story, where P1 is the most critical.
  Think of each story as a standalone slice of functionality that can be:
  - Developed independently
  - Tested independently
  - Deployed independently
  - Demonstrated to users independently
-->

### User Story 1 - Build Reusable Hardware Context (Priority: P1)

As a firmware maintainer adding support for a new device, I want a single repository-backed reference
that summarizes the common board-definition inputs already used across Meshtastic targets so I can
start from verified patterns instead of re-deriving pins, feature flags, and board metadata from scratch.

**Why this priority**: Without an authoritative context source, any later agent workflow will repeat the
same manual discovery work and risks copying incorrect or incomplete target definitions.

**Independent Test**: Can be fully tested by generating the hardware context artifact from the current
repository and confirming it captures existing target-definition fields, common pins, and board-scoped
capability patterns for a representative set of boards.

**Acceptance Scenarios**:

1. **Given** an existing firmware checkout with multiple board variants, **When** a maintainer requests
   the hardware support context, **Then** the system produces a markdown artifact that documents current
   board-definition inputs, common pin categories, and recurring variant-level capabilities from the repo.
2. **Given** a maintainer reviewing an existing board, **When** they inspect the context artifact,
   **Then** they can identify the board environment, hardware model identifiers, display-related fields,
   radio pin definitions, input pins, and other commonly reused target-definition elements without
   manually scanning many variant files.

---

### User Story 2 - Define New Board Intake (Priority: P2)

As a firmware maintainer, I want to provide a small set of board inputs such as environment name,
hardware model, display name, and source materials so the Copilot workflow can determine what new
hardware support artifacts need to be created or filled in.

**Why this priority**: A constrained intake contract is required before the workflow can safely generate
variant files and PlatformIO environments for new hardware support.

**Independent Test**: Can be tested independently by supplying the declared board inputs for a hypothetical
new target and confirming the workflow identifies required artifacts, missing evidence, and board-definition
fields that must be resolved before code generation proceeds.

**Acceptance Scenarios**:

1. **Given** a maintainer provides an environment name, hardware model, display name, and source references,
   **When** the intake workflow runs, **Then** it identifies the expected target-definition artifacts,
   required metadata fields, and unresolved board details that still need confirmation.
2. **Given** the supplied materials do not establish enough hardware truth for safe generation,
   **When** the workflow evaluates the request, **Then** it explicitly flags the missing pin mappings,
   peripheral capabilities, or board metadata instead of guessing silently.

---

### User Story 3 - Generate Board Support Scaffolding (Priority: P3)

As a firmware maintainer, I want the Copilot workflow to use the validated intake and reusable context to
draft board-support artifacts such as `variant.h`, optional `variant.cpp`, and PlatformIO environment content
with repository-specific metadata so I can add new hardware support consistently and with less manual setup.

**Why this priority**: This delivers the actual acceleration benefit, but it depends on verified context and
safe intake rules to avoid generating incorrect hardware support.

**Independent Test**: Can be tested independently by running the workflow for a new board request and
confirming it produces scaffold content or structured instructions that align with repository patterns and
does not invent unsupported hardware details.

**Acceptance Scenarios**:

1. **Given** validated board inputs and sufficient source evidence, **When** the maintainer requests new
   hardware support scaffolding, **Then** the workflow drafts the required board-support files and metadata
   using existing repository conventions.
2. **Given** the request would affect hardware flags, pin mappings, or build metadata beyond the available
   evidence, **When** scaffolding is attempted, **Then** the workflow limits output to supported fields and
   clearly marks unresolved items for human confirmation.

---

### Edge Cases

- The requested board environment name conflicts with an existing PlatformIO environment or target directory.
- The same hardware model appears under multiple existing naming conventions and the correct repository form is ambiguous.
- A schematic or PDF source omits some pins or names them differently than the repository’s existing macros.
- A target uses architecture defaults today, so the context artifact must distinguish explicitly declared pins from inherited defaults.
- A board has multiple display or radio options, optional peripherals, or revision-specific pinouts that cannot be collapsed into one truth.
- The request includes peripherals that exist in source material but are not currently supported by repository patterns.
- A generated board definition would require changing shared defaults or introducing unverified power, timing, or RF assumptions.

## Requirements _(mandatory)_

### Functional Requirements

- **FR-001**: The system MUST produce a repository-local hardware context artifact that documents the current
  target-definition patterns used for board support in this firmware repository.
- **FR-002**: The hardware context artifact MUST describe, at minimum, the board environment name,
  hardware model identifier, display-related identifiers, radio pin group, input/button-related pins,
  and other commonly reused pin or capability categories present at the device-target-definition level.
- **FR-003**: The hardware context artifact MUST distinguish board-specific declarations from architecture-level
  defaults or inherited behavior when that distinction affects new board support work.
- **FR-004**: Users MUST be able to initiate the workflow by providing a minimal set of board inputs that includes
  board environment name, hardware model, display name, and target architecture, with optional supporting source materials.
  Architecture is required because expected artifacts and matched patterns cannot be determined without it.
- **FR-005**: The intake workflow MUST identify which board-support artifacts are expected for the request,
  including variant files and PlatformIO environment content where applicable.
- **FR-006**: The intake workflow MUST surface missing or conflicting hardware evidence instead of inventing
  unresolved pins, capabilities, metadata, or power assumptions.
- **FR-007**: The workflow MUST preserve variant-scoped hardware truth by keeping generated or suggested values
  scoped to the intended target architecture, board, and board revision when known.
- **FR-008**: The workflow MUST support repository-specific metadata required for new PlatformIO environments,
  including custom support metadata already used in this codebase.
- **FR-009**: The workflow MUST be able to draft scaffold content for `variant.h`, optional `variant.cpp`, and
  related target files only when the provided evidence is sufficient to do so safely.
- **FR-010**: The workflow MUST record unresolved questions in a form the maintainer can act on before using any
  scaffolded board support in the repository.
- **FR-011**: The workflow MUST be applicable to current Meshtastic hardware target definitions across supported
  architectures, while allowing architecture-specific details to remain architecture-scoped.
- **FR-012**: The workflow MUST not change public protocol behavior, shared radio safety defaults, or unrelated
  board definitions as part of preparing new hardware support context.

Where relevant, requirements MUST also state:

- affected architectures, boards, or modules: ESP32, ESP32-S3, ESP32-C3, nRF52, RP2040/RP2350, STM32WL, and Portduino-style target-definition patterns where present in repo conventions
- whether behavior changes public defaults, protocol compatibility, or generated artifacts: this feature must not alter public mesh defaults or protocol compatibility; it may create or update documentation artifacts for board-support workflow context
- any required validation evidence for high-risk mesh, hardware, or power behavior: targeted verification of generated context against representative variant files and naming patterns is required before relying on it for scaffolding

### Key Entities _(include if feature involves data)_

- **Hardware Support Context**: A repository-backed reference artifact that summarizes the current board-support
  fields, common pin categories, variant capability macros, and target-definition conventions used across the firmware.
- **Board Intake Request**: The maintainer-provided input set for a proposed new board, including environment name,
  hardware model, display name, target architecture, and optional source materials such as schematics.
- **Target Definition Pattern**: A reusable repository convention describing how a board is represented through
  `variant.h`, optional companion files, PlatformIO environments, and board-specific metadata.
- **Evidence Gap**: A missing, ambiguous, or conflicting hardware fact that blocks safe generation of new board support.
- **Board Support Scaffold**: The draft output for new hardware support artifacts, limited to fields supported by
  verified evidence and current repository conventions.

## Success Criteria _(mandatory)_

### Measurable Outcomes

- **SC-001**: Maintainers can locate the common target-definition inputs and pin categories for an existing board in one artifact within 5 minutes, without manually reading multiple variant directories.
- **SC-002**: For a representative set of existing boards, the context artifact correctly captures the board environment name, key capability categories, and primary pin groups with no unresolved mismatches after maintainer review.
- **SC-003**: A maintainer can submit a new board intake request using the declared minimum inputs and receive a complete list of required board-support artifacts and unresolved evidence gaps in a single workflow pass.
- **SC-004**: _(Post-launch metric — not a pre-merge acceptance gate)_ For new board requests with sufficient source evidence, the workflow reduces manual setup time for initial board-support scaffolding by at least 50% compared with manually assembling variant and PlatformIO definitions from scratch. A baseline timed comparison should be conducted after the first production intake.

## Assumptions

- The initial increment focuses on repository context and intake/scaffolding workflow support, not full autonomous end-to-end board enablement.
- Maintainers using this workflow already have access to the repository, Copilot, and any board source materials they want to reference.
- The first implementation may rely on repository-readable sources and manually supplied board details rather than automated PDF parsing.
- Existing Meshtastic variant files and PlatformIO environments provide enough representative patterns to build a useful reusable context artifact.
- The workflow will be allowed to stop and request clarification when hardware truth is incomplete instead of forcing a guessed output.
