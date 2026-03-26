<!--
Sync Impact Report
Version change: template -> 1.0.0
Modified principles:
- [PRINCIPLE_1_NAME] -> I. Safety-Critical Mesh Behavior
- [PRINCIPLE_2_NAME] -> II. Variant-Scoped Hardware Truth
- [PRINCIPLE_3_NAME] -> III. Verification by Targeted Evidence
- [PRINCIPLE_4_NAME] -> IV. Resource and Power Discipline
- [PRINCIPLE_5_NAME] -> V. Minimal, Reviewable Change Sets
Added sections:
- Engineering Constraints
- Delivery Workflow
Removed sections:
- None
Templates requiring updates:
- ✅ .specify/templates/plan-template.md
- ✅ .specify/templates/spec-template.md
- ✅ .specify/templates/tasks-template.md
- ⚠ pending .specify/templates/checklist-template.md (not required for current constitution alignment)
- ⚠ pending .specify/templates/agent-file-template.md (generic scaffold, no constitution-specific drift found)
Follow-up TODOs:
- None
-->

# Meshtastic Firmware Constitution

## Core Principles

### I. Safety-Critical Mesh Behavior

All feature and bug-fix work MUST preserve safe, predictable behavior on live mesh networks.
Changes that affect routing, airtime, broadcast intervals, MQTT bridging, channel handling,
or packet processing MUST document the operational impact on shared bandwidth, public-channel
abuse protections, and interoperability. Any relaxation of rate limits, encryption behavior,
or default-channel safeguards MUST be treated as a breaking governance change unless explicitly
approved and justified.

Rationale: Meshtastic devices operate in constrained radio environments where seemingly small
behavior changes can degrade network reliability, privacy, and fairness for other nodes.

### II. Variant-Scoped Hardware Truth

Board definitions, platform conditionals, and peripheral flags MUST reflect verified hardware
truth and MUST remain scoped to the exact supported target. New capabilities in `variant.h`,
`platformio.ini`, `pins_arduino.h`, or related board files MUST be backed by pin mappings,
chip selection, and power assumptions that are consistent with the board design. Cross-board
copying is prohibited unless every reused define is revalidated for the destination variant.

Rationale: This firmware spans many architectures and board revisions; incorrect hardware
declarations create silent regressions that are hard to detect until devices are flashed.

### III. Verification by Targeted Evidence

Every change MUST be validated by the smallest credible evidence that matches its risk.
At minimum, contributors MUST run formatting or static validation for touched files and MUST
run a targeted build, test, or simulation path for the affected platform when feasible.
Changes to shared core logic, protobufs, routing, or configuration defaults SHOULD include a
native test, simulator run, or equivalent cross-target evidence. If validation cannot be run,
the gap MUST be stated explicitly in the plan, tasks, and final review.

Rationale: The repository supports many targets, so quality depends on explicit validation
rather than assumptions that one successful build implies system-wide safety.

### IV. Resource and Power Discipline

Implementations MUST respect embedded constraints for memory, flash, CPU, battery, and radio
duty cycle. New dependencies, background tasks, logging, polling, display work, and peripheral
power use MUST be justified against the target hardware footprint. Defaults MUST prefer safe
operation on constrained devices, and network-facing behavior MUST account for scaling with
node count where existing project patterns provide that mechanism.

Rationale: Meshtastic firmware runs on low-power devices where unnecessary work directly harms
battery life, responsiveness, thermal behavior, and mesh capacity.

### V. Minimal, Reviewable Change Sets

Changes MUST solve the root problem with the smallest coherent diff that fits the existing
architecture and coding patterns. Unrelated refactors, opportunistic renames, and speculative
abstractions are prohibited in the same change unless they are required to make the fix safe.
Public behavior, configuration semantics, and generated artifacts MUST remain stable unless the
specification and plan explicitly call out the intended change.

Rationale: Small, scoped changes are easier to review across board variants and reduce the
risk of hidden regressions in a large multi-platform firmware repository.

## Engineering Constraints

The authoritative implementation context for this repository is `.github/copilot-instructions.md`.
Plans and tasks MUST align with the existing PlatformIO-based build system, generated protobuf
workflow, architecture-specific source layout, and hardware-variant structure already used in
the repository.

Feature work MUST honor these constraints:

- Code MUST follow existing logging, naming, threading, and configuration patterns.
- Default values and user-facing configuration changes MUST use existing `Default` helpers and
  public-channel safeguards where applicable.
- Protobuf changes MUST include regeneration steps and identify downstream effects in generated
  code and dependent modules.
- Build and validation steps MUST prefer repository-standard commands such as targeted `pio run`,
  `pio test -e native`, simulator tooling, and `trunk fmt` where applicable.
- Platform-specific logic MUST be isolated to the narrowest valid architecture or variant scope.

## Delivery Workflow

Spec-driven work in this repository MUST produce artifacts that make operational risk visible
before code is written.

- Specifications MUST describe affected user or device behavior, impacted platforms, and edge
  cases for unavailable peripherals, misconfigured variants, and constrained-network scenarios.
- Plans MUST include a Constitution Check that names the exact validation evidence, target
  environments, and any justified deviations from the constitution.
- Tasks MUST be organized so that foundational hardware, protocol, or configuration work is
  completed before feature-specific behavior that depends on it.
- Review and implementation notes MUST call out any skipped validation, generated-file updates,
  migration considerations, or behavior changes requiring maintainer scrutiny.

## Governance

This constitution supersedes ad hoc workflow preferences for spec-driven work in this repository.
All plans, tasks, reviews, and implementation summaries MUST verify compliance with these
principles.

- Amendments MUST be documented in this file and reflected in dependent templates before new
  work proceeds under the changed rule set.
- Versioning policy for this constitution follows semantic versioning.
  MAJOR versions indicate removed or materially redefined principles.
  MINOR versions indicate new principles, sections, or materially expanded guidance.
  PATCH versions indicate clarifications, wording improvements, or non-semantic refinements.
- Compliance review is mandatory for every feature plan and code review. Any constitutional
  violation MUST be listed in the plan's Complexity Tracking section or equivalent justification.
- Repository guidance files remain authoritative for implementation detail. Where conflict is
  discovered, this constitution governs process and quality gates, while `.github/copilot-instructions.md`
  governs repository-specific coding practice until the conflict is resolved by amendment.

**Version**: 1.0.0 | **Ratified**: 2026-03-25 | **Last Amended**: 2026-03-25
