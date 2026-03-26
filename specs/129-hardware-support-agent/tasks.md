# Tasks: Hardware Support Agent

**Branch**: `129-hardware-support-agent`
**Input**: Design documents from `specs/129-hardware-support-agent/`
**Prerequisites**: plan.md ✅, spec.md ✅, research.md ✅, data-model.md ✅, contracts/board-intake-contract.md ✅

**Note on existing work**: The Phase 3 (US1) inventory script and generated context artifact already exist.
Tasks T001–T004 treat them as in-scope for validation and hardening rather than creation from scratch.

---

## Phase 1: Setup

**Purpose**: Confirm repository structure and tooling baseline for this feature.

- [x] T001 Confirm `bin/generate_hardware_support_context.py` executes without errors from the repo root via `python3 bin/generate_hardware_support_context.py`
- [x] T002 [P] Confirm `docs/hardware-support-context.md` exists and is committed or tracked in the working tree
- [x] T003 [P] Clean up the duplicated `"Where relevant, requirements MUST also state:"` block in `specs/129-hardware-support-agent/spec.md`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared utilities and data structures that US2 and US3 both depend on.

**⚠️ CRITICAL**: US2 and US3 cannot begin until this phase is complete.

- [x] T004 Add a `BoardIntakeRequest` dataclass (or typed dict) capturing the fields from `specs/129-hardware-support-agent/data-model.md` in `bin/board_intake.py`
- [x] T005 Add a `EvidenceGap` dataclass in `bin/board_intake.py` matching the data model
- [x] T006 Add an `IntakeAssessment` dataclass in `bin/board_intake.py` matching the data model
- [x] T007 Implement a `load_hardware_context(path)` helper in `bin/board_intake.py` that reads `docs/hardware-support-context.md` and returns architecture family names and metadata key list for use by the assessment logic

**Checkpoint**: Shared data structures and context loader in place — US2 and US3 implementation can begin.

---

## Phase 3: User Story 1 — Build Reusable Hardware Context (Priority: P1) 🎯 MVP

**Goal**: A single repository-backed markdown document accurately inventories current board-support patterns across all supported architectures so maintainers can start a new board definition from verified patterns.

**Independent Test**: Run `python3 bin/generate_hardware_support_context.py` and manually spot-check output against four representative variants from different architectures.

### Validation for User Story 1

- [x] T008 [P] [US1] Spot-check generated `docs/hardware-support-context.md` against `variants/esp32/tbeam` — confirm radio pins, metadata keys, and category counts match the variant.h and platformio.ini declarations
- [x] T009 [P] [US1] Spot-check against `variants/esp32s3/tlora-pager` — confirm all 17 `custom_meshtastic_*` metadata keys and high Connectivity/Other category macro count are reflected
- [x] T010 [P] [US1] Spot-check against `variants/nrf52840/t-echo` — confirm nRF52-specific macros (`USE_LFXO`, `VARIANT_MCK`, nRF52-style SPI pins) appear correctly
- [x] T011 [P] [US1] Spot-check against `variants/rp2040/rak11310` — confirm RP2040 architecture entry is present and radio pins match
- [x] T012 [US1] Run `trunk fmt bin/generate_hardware_support_context.py` and fix any formatting issues

### Implementation for User Story 1

- [x] T013 [P] [US1] Add `--validate` CLI flag to `bin/generate_hardware_support_context.py` that prints a summary of how many variants were scanned, how many had metadata keys, and how many had no `variant.h` (for inherited-defaults audit)
- [x] T014 [US1] Add a `## Inherited Defaults Note` section to the generated `docs/hardware-support-context.md` that lists architecture families known to rely on BSP/base-environment defaults rather than locally declared macros (informed by validate output)

**Checkpoint**: `docs/hardware-support-context.md` is validated, formatted, and includes inherited-defaults guidance. US1 fully testable and deliverable independently.

---

## Phase 4: User Story 2 — Define New Board Intake (Priority: P2)

**Goal**: A maintainer can provide an environment name, hardware model, display name, and architecture and receive back a structured assessment listing expected artifacts, required metadata, closest matching patterns, and any evidence gaps.

**Independent Test**: Run the intake workflow against a sample hypothetical board (e.g., a new ESP32-S3 board with only minimum inputs) and confirm it returns a complete assessment with at least one evidence gap identified.

### Validation for User Story 2

- [x] T015 [P] [US2] Create `bin/fixtures/intake_minimal.json` with just the required fields for a hypothetical new ESP32-S3 board and confirm `bin/board_intake.py` parses it without error
- [x] T016 [P] [US2] Create `bin/fixtures/intake_full.json` with all recommended fields and source materials and confirm the workflow marks `scaffold_ready: true`
- [x] T016b [P] [US2] Create `bin/fixtures/intake_multi_env.json` representing a board with two display variants sharing one hardware model (e.g., a TFT and an e-ink variant) and confirm `assess_intake` flags `revision-scope` ambiguity as a blocking Evidence Gap rather than collapsing the options silently
- [x] T017 [US2] Confirm `bin/board_intake.py --validate bin/fixtures/intake_minimal.json` correctly flags missing radio, display, and power evidence as blocking gaps

### Implementation for User Story 2

- [x] T018 [P] [US2] Implement `validate_intake(request: BoardIntakeRequest) -> list[str]` in `bin/board_intake.py` that checks required fields and detects environment-name conflicts against the existing architecture inventory in `docs/hardware-support-context.md`
- [x] T019 [P] [US2] Implement `find_matched_patterns(request: BoardIntakeRequest, context) -> list[dict]` that returns the three closest existing board examples by architecture from the context document
- [x] T020 [US2] Implement `build_evidence_gaps(request: BoardIntakeRequest) -> list[EvidenceGap]` that identifies missing pin group evidence (radio, display, GPS, power, input) based on declared source materials
- [x] T021 [US2] Implement `assess_intake(request: BoardIntakeRequest, context) -> IntakeAssessment` combining T018–T020 to produce a full structured assessment
- [x] T022 [US2] Implement `render_assessment_markdown(assessment: IntakeAssessment) -> str` that formats the assessment as a maintainer-readable markdown report
- [x] T023 [US2] Add a CLI entry point `bin/board_intake.py <intake.json>` that prints the assessment markdown to stdout or an output file
- [x] T024 [US2] Run `trunk fmt bin/board_intake.py` and fix any formatting issues

**Checkpoint**: `bin/board_intake.py` fully processes a new board request and prints an assessment. US2 independently testable.

---

## Phase 5: User Story 3 — Generate Board Support Scaffolding (Priority: P3)

**Goal**: When intake assessment marks `scaffold_ready: true`, the workflow drafts `variant.h`, optional `variant.cpp`, and PlatformIO environment content using repository conventions, with inline annotations for unresolved items.

**Independent Test**: Run the scaffold generator for a fully-specified test case and confirm the output files follow existing repository patterns and include `// TODO:` markers for any fields not backed by supplied evidence.

### Validation for User Story 3

- [x] T025 [P] [US3] Run scaffold generator for `bin/fixtures/intake_full.json` and confirm `variant.h` output contains required radio pin group, capability macros, and metadata section
- [x] T026 [P] [US3] Confirm scaffold generator emits `// TODO: verify —` annotations for any recommended fields absent from the intake fixture
- [x] T027 [US3] Confirm generated PlatformIO env block includes all `custom_meshtastic_*` metadata keys from the contract and uses `extends` to reference the correct base environment for the declared architecture

### Implementation for User Story 3

- [x] T028 [P] [US3] Create `bin/board_scaffold.py` with a `generate_variant_h(assessment: IntakeAssessment, context) -> str` function that drafts a `variant.h` file using architecture-appropriate macro order from the context document
- [x] T029 [P] [US3] Implement `generate_platformio_env(assessment: IntakeAssessment) -> str` in `bin/board_scaffold.py` that emits the PlatformIO environment block with `custom_meshtastic_*` fields
- [x] T030 [US3] Implement `annotate_unresolved(content: str, gaps: list[EvidenceGap]) -> str` that inserts `// TODO: verify — {gap.description}` comments next to lines that correspond to unresolved evidence gaps
- [x] T031 [US3] Implement `scaffold_board(assessment: IntakeAssessment, context, output_dir: Path)` which orchestrates T028–T030 and writes files to the proposed variant directory path
- [x] T032 [US3] Add CLI entry point `bin/board_scaffold.py <intake.json> [--output-dir <path>]` that reads an intake file, runs the full intake + scaffold pipeline, and writes output
- [x] T033 [US3] Guard scaffold entry: if `assess_intake` returns `scaffold_ready: false`, print the assessment report and exit with a non-zero code rather than generating files
- [x] T034 [US3] Run `trunk fmt bin/board_scaffold.py` and fix any formatting issues

**Checkpoint**: All three user stories independently functional. Hardware context, intake assessment, and scaffold generation each work as standalone deliverables.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Custom Copilot agent integration, documentation, and final review.

- [x] T035 [P] Create `.github/prompts/hardware-support.prompt.md` that routes the hardware support workflow to the custom agent
- [x] T036 [P] Create `.github/agents/hardware-support.agent.md` with step-by-step instructions for the Copilot hardware-support workflow: context regeneration → intake → assessment → optional scaffold
- [x] T037 [US1] Update `specs/129-hardware-support-agent/quickstart.md` Step 3 and Step 4 to reference the completed `bin/board_intake.py` CLI and describe the expected output shape
- [x] T038 [P] Confirm that no firmware runtime files under `src/`, `variants/`, or `protobufs/` were modified as part of this feature (constitution compliance check)
- [x] T039 Run `python3 bin/generate_hardware_support_context.py` one final time to confirm the regenerated artifact is up to date and passes spot-checks from T008–T011
- [x] T040 Summarize any skipped validations, open `// TODO:` items in scaffold output, and any known limitations in `specs/129-hardware-support-agent/plan.md` under a `## Review Notes` section

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 — blocks US2 and US3
- **Phase 3 (US1)**: Can start after Phase 1; does not depend on Phase 2
- **Phase 4 (US2)**: Depends on Phase 2 (shared data structures)
- **Phase 5 (US3)**: Depends on Phase 2 and Phase 4 (uses IntakeAssessment output)
- **Phase 6 (Polish)**: Depends on all story phases

### User Story Dependencies

- **US1 (P1)**: Depends on Phase 1 only — fully independent MVP
- **US2 (P2)**: Depends on Phase 2; integrates with US1 context document but not the script directly
- **US3 (P3)**: Depends on Phase 2 and US2 `assess_intake` result

### Parallel Opportunities

**Within Phase 2**:

- T004, T005, T006 can be written in parallel (same file, separate dataclasses — serialize to avoid conflicts)

**Within Phase 3 (US1)**:

- T008, T009, T010, T011 (spot-checks) are fully independent and can run in parallel
- T013 [P] is independent of validation tasks

**Within Phase 4 (US2)**:

- T015, T016 fixture creation can run in parallel
- T018 and T019 are independent of each other and can be implemented in parallel

**Within Phase 5 (US3)**:

- T028 and T029 are independent scaffold generators — can be written in parallel
- T025 and T026 validation tasks are independent

**Within Phase 6**:

- T035, T036, T038 are independent files — can all be done in parallel

### MVP Scope

To deliver US1 as a standalone MVP: complete Phases 1 and 3 (T001–T003, T008–T014). This verifies the hardware context artifact is accurate and useful without requiring any intake or scaffold tooling.
