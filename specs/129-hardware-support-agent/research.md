# Research: Hardware Support Agent

## Decision: Use repository-local Python tooling and markdown artifacts as the first implementation slice

Rationale: The repository already uses lightweight scripts under `bin/` and maintains board truth primarily in `variants/**/platformio.ini` and `variants/**/variant.h`. A Python script plus markdown outputs fits existing repo patterns, keeps dependencies minimal, and provides immediate value without touching firmware runtime code.

Alternatives considered:

- Implement the first increment directly as a full custom Copilot agent that generates new board files. Rejected because the workflow still needs a safer evidence-validation layer before scaffolding hardware definitions.
- Build a standalone service or extension-backed parser. Rejected because it would add unnecessary complexity, operational overhead, and dependencies for a repo-local maintainer workflow.

## Decision: Treat intake validation and readiness reporting as the first generation boundary

Rationale: The constitution requires verified, variant-scoped hardware truth and forbids guessing pins or capabilities. A report-first boundary lets maintainers capture missing evidence, expected artifacts, and metadata requirements before draft files are produced.

Alternatives considered:

- Generate `variant.h` and `platformio.ini` scaffolding immediately from minimum inputs. Rejected because environment name, `hw_model`, and display name are not enough to guarantee correct radio, power, display, and peripheral mappings.
- Block all workflow progress until every future scaffold field is known. Rejected because maintainers still need a useful way to understand what is missing and what repository patterns apply.

## Decision: Reuse the existing hardware inventory document as the canonical context input for planning

Rationale: `docs/hardware-support-context.md` already inventories metadata keys, common macro categories, and representative board examples across the repository. That artifact can serve as the phase 1 foundation for both maintainers and future agent prompts.

Alternatives considered:

- Generate a new per-architecture context file for each family. Rejected for now because a single canonical context artifact is easier to review and sufficient for the first intake/report workflow.
- Depend on maintainers manually browsing variant files during intake. Rejected because it defeats the feature goal of reducing rediscovery work.

## Decision: Represent the maintainer workflow as a small set of explicit entities and a human-readable contract

Rationale: The feature is primarily a repository workflow, not a networked API. A markdown contract describing required fields, validations, and outputs is sufficient for Spec Kit design and future agent/prompt implementation.

Alternatives considered:

- Define a JSON Schema or OpenAPI contract immediately. Rejected for phase 1 because no external service boundary exists yet and the workflow is still evolving.
- Keep the contract implicit in prompt text only. Rejected because it would be harder to review, test, and keep aligned with the constitution.

## Decision: Keep scaffold generation as a designed later phase gated by evidence sufficiency

Rationale: The user wants the end state to include new board-support scaffolding, but constitutional safety requires a stronger intake and validation model first. Planning the later phase now preserves momentum without collapsing safe and unsafe scopes together.

Alternatives considered:

- Remove scaffolding from the feature entirely. Rejected because it is core to the requested long-term outcome.
- Merge report generation and scaffolding into a single undifferentiated phase. Rejected because it weakens reviewability and blurs the safety boundary.
