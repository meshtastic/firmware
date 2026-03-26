#!/usr/bin/env python3
"""Board intake assessment for new Meshtastic hardware support.

Validates a board intake request against the repository hardware context,
identifies evidence gaps, and produces a structured readiness report.

Usage:
    python3 bin/board_intake.py <intake.json>
    python3 bin/board_intake.py <intake.json> --output report.md
    python3 bin/board_intake.py <intake.json> --validate   # gaps check only, exit 1 if not scaffold_ready
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONTEXT_PATH = ROOT / "docs" / "hardware-support-context.md"

# Metadata keys every new PlatformIO environment should declare.
REQUIRED_METADATA_KEYS = [
    "custom_meshtastic_hw_model",
    "custom_meshtastic_hw_model_slug",
    "custom_meshtastic_architecture",
    "custom_meshtastic_actively_supported",
    "custom_meshtastic_support_level",
    "custom_meshtastic_display_name",
]

RECOMMENDED_METADATA_KEYS = [
    "custom_meshtastic_images",
    "custom_meshtastic_tags",
    "custom_meshtastic_requires_dfu",
    "custom_meshtastic_partition_scheme",
]

# Pin groups that should be backed by evidence before scaffolding.
EVIDENCE_CATEGORIES = ["radio", "display", "input", "GPS", "power"]

# Architecture families that rely on BSP defaults for many pin defines.
BSP_DEFAULT_FAMILIES = {"nrf52840", "rp2040", "stm32", "native"}

# Known valid architectures from the repository.
KNOWN_ARCHITECTURES = {
    "esp32",
    "esp32-s3",
    "esp32-c3",
    "esp32-c6",
    "esp32s2",
    "nrf52840",
    "rp2040",
    "rp2350",
    "stm32",
    "native",
}


# ---------------------------------------------------------------------------
# T004 – BoardIntakeRequest dataclass
# ---------------------------------------------------------------------------


@dataclass
class BoardIntakeRequest:
    """Maintainer-supplied description of a proposed new board."""

    # Required
    environment_name: str
    hardware_model: str
    display_name: str
    architecture: str

    # Recommended
    hardware_model_slug: str = ""
    actively_supported: bool | None = None
    support_level: str = ""
    source_materials: list[str] = field(default_factory=list)
    board_notes: str = ""

    @classmethod
    def from_dict(cls, data: dict) -> "BoardIntakeRequest":
        return cls(
            environment_name=data.get("environment_name", ""),
            hardware_model=str(data.get("hardware_model", "")),
            display_name=data.get("display_name", ""),
            architecture=data.get("architecture", ""),
            hardware_model_slug=data.get("hardware_model_slug", ""),
            actively_supported=data.get("actively_supported"),
            support_level=str(data.get("support_level", "")),
            source_materials=list(data.get("source_materials", [])),
            board_notes=data.get("board_notes", ""),
        )

    @classmethod
    def from_json(cls, path: Path) -> "BoardIntakeRequest":
        data = json.loads(path.read_text(encoding="utf-8"))
        return cls.from_dict(data)


# ---------------------------------------------------------------------------
# T005 – EvidenceGap dataclass
# ---------------------------------------------------------------------------


@dataclass
class EvidenceGap:
    """A specific missing, conflicting, or ambiguous hardware fact."""

    category: str  # metadata | radio | display | input | GPS | power | storage | connectivity | revision-scope
    description: str
    affected_artifact: str
    required_evidence: str
    blocking: bool


# ---------------------------------------------------------------------------
# T006 – IntakeAssessment dataclass
# ---------------------------------------------------------------------------


@dataclass
class IntakeAssessment:
    """Structured result of evaluating a BoardIntakeRequest."""

    request: BoardIntakeRequest
    expected_artifacts: list[str]
    required_metadata: list[str]
    matched_patterns: list[dict]
    evidence_gaps: list[EvidenceGap]
    risk_flags: list[str]
    next_actions: list[str]
    scaffold_ready: bool


# ---------------------------------------------------------------------------
# T007 – load_hardware_context
# ---------------------------------------------------------------------------


def load_hardware_context(path: Path = DEFAULT_CONTEXT_PATH) -> dict:
    """Parse the generated hardware-support-context.md into a usable dict.

    Returns:
        {
            "architecture_names": list[str],  # families present in the inventory
            "metadata_keys": list[str],       # custom_meshtastic_* keys observed
            "environments": dict[str, dict],  # env_name -> {display, hw_model, hw_slug, variant_dir, arch}
        }
    """
    if not path.exists():
        raise FileNotFoundError(
            f"Hardware context not found at {path}. "
            "Run: python3 bin/generate_hardware_support_context.py"
        )

    text = path.read_text(encoding="utf-8")

    # Extract metadata keys from the "## Repository Metadata Inputs" section.
    metadata_keys: list[str] = re.findall(r"`(custom_meshtastic_[^`]+)`", text)
    metadata_keys = list(dict.fromkeys(metadata_keys))  # deduplicate, preserve order

    # Extract architecture families from the "## Architecture and Environment Inventory" section.
    arch_names: list[str] = re.findall(r"^### ([a-z0-9\-]+)\s*$", text, re.MULTILINE)
    # Filter out sub-headings that are architecture names (exclude e.g. "nrf52840" inside examples)
    # The inventory section has short arch names; filter to known set plus any that look like archs.
    arch_names = [a for a in dict.fromkeys(arch_names) if not a[0].isupper()]

    # Extract environments from inventory table rows.
    environments: dict[str, dict] = {}
    current_arch = ""
    for line in text.splitlines():
        arch_match = re.match(r"^### ([a-z0-9\-]+)\s*$", line)
        if arch_match:
            current_arch = arch_match.group(1)
            continue
        # Table row: | env | display | hw_model | hw_slug | variant_dir | categories |
        row = re.match(
            r"^\|\s*([^|]+?)\s*\|\s*([^|]*?)\s*\|\s*([^|]*?)\s*\|\s*([^|]*?)\s*\|\s*([^|]*?)\s*\|",
            line,
        )
        if (
            row
            and not row.group(1).startswith("Environment")
            and not row.group(1).startswith("---")
        ):
            env_name = row.group(1).strip()
            if env_name:
                environments[env_name] = {
                    "display_name": row.group(2).strip(),
                    "hw_model": row.group(3).strip(),
                    "hw_slug": row.group(4).strip(),
                    "variant_dir": row.group(5).strip(),
                    "architecture": current_arch,
                }

    return {
        "architecture_names": arch_names,
        "metadata_keys": metadata_keys,
        "environments": environments,
    }


# ---------------------------------------------------------------------------
# T018 – validate_intake
# ---------------------------------------------------------------------------


def validate_intake(request: BoardIntakeRequest, context: dict) -> list[str]:
    """Check required fields and detect conflicts with existing environments/models.

    Returns a list of validation error strings (empty = valid).
    """
    errors: list[str] = []

    if not request.environment_name:
        errors.append("environment_name is required.")
    if not request.hardware_model:
        errors.append("hardware_model is required.")
    if not request.display_name:
        errors.append("display_name is required.")
    if not request.architecture:
        errors.append("architecture is required.")

    if request.architecture and request.architecture not in KNOWN_ARCHITECTURES:
        errors.append(
            f"architecture '{request.architecture}' is not a known repository architecture. "
            f"Known: {', '.join(sorted(KNOWN_ARCHITECTURES))}"
        )

    # Conflict: environment name already exists
    if request.environment_name and request.environment_name in context.get(
        "environments", {}
    ):
        errors.append(
            f"environment_name '{request.environment_name}' already exists in the repository. "
            "Choose a unique name or confirm this is an intentional update."
        )

    # Conflict: hardware model already assigned to a different environment
    if request.hardware_model:
        for env_name, env_data in context.get("environments", {}).items():
            if (
                env_data.get("hw_model") == request.hardware_model
                and env_name != request.environment_name
            ):
                errors.append(
                    f"hardware_model '{request.hardware_model}' is already assigned to "
                    f"environment '{env_name}' ({env_data.get('display_name', '')!r}). "
                    "Verify this is a new model number or confirm the shared-model intent."
                )
                break  # report once

    return errors


# ---------------------------------------------------------------------------
# T019 – find_matched_patterns
# ---------------------------------------------------------------------------


def find_matched_patterns(request: BoardIntakeRequest, context: dict) -> list[dict]:
    """Return up to 3 existing environments closest to the request by architecture."""
    envs = context.get("environments", {})
    arch = request.architecture

    # Prefer exact architecture match, then partial (e.g., "esp32" matches "esp32-s3").
    exact: list[dict] = []
    partial: list[dict] = []
    for env_name, env_data in envs.items():
        entry = {**env_data, "environment": env_name}
        env_arch = env_data.get("architecture", "")
        if env_arch == arch:
            exact.append(entry)
        elif arch and (env_arch.startswith(arch) or arch.startswith(env_arch)):
            partial.append(entry)

    candidates = exact + partial
    # Prefer boards that have a display_name and hw_model (more complete entries).
    candidates.sort(key=lambda e: (not e.get("display_name"), not e.get("hw_model")))
    return candidates[:3]


# ---------------------------------------------------------------------------
# T020 – build_evidence_gaps
# ---------------------------------------------------------------------------


def build_evidence_gaps(request: BoardIntakeRequest) -> list[EvidenceGap]:
    """Identify missing pin-group evidence and metadata gaps."""
    gaps: list[EvidenceGap] = []
    has_sources = bool(request.source_materials)

    if not has_sources:
        # Every pin category is unresolvable without sources.
        for cat in EVIDENCE_CATEGORIES:
            gaps.append(
                EvidenceGap(
                    category=cat,
                    description=(
                        f"No source materials supplied. {cat.capitalize()} pin mappings cannot be "
                        "verified without a schematic, pinout diagram, or vendor datasheet."
                    ),
                    affected_artifact="variant.h",
                    required_evidence="Schematic, pinout image, or vendor board page",
                    blocking=True,
                )
            )
    else:
        # Sources exist but may still be incomplete; flag as non-blocking advisory.
        for cat in EVIDENCE_CATEGORIES:
            gaps.append(
                EvidenceGap(
                    category=cat,
                    description=(
                        f"Source materials are present but {cat} pin assignments have not been "
                        "extracted and cross-checked against repository macro conventions."
                    ),
                    affected_artifact="variant.h",
                    required_evidence=f"Explicit {cat} pin listing matched to repository #define names",
                    blocking=False,
                )
            )

    # Metadata gaps
    if not request.hardware_model_slug:
        gaps.append(
            EvidenceGap(
                category="metadata",
                description="hardware_model_slug is not set. The repository requires an UPPER_SNAKE_CASE slug for PlatformIO metadata.",
                affected_artifact="platformio.ini (custom_meshtastic_hw_model_slug)",
                required_evidence="Agreed slug from the project maintainers",
                blocking=True,
            )
        )

    if request.actively_supported is None:
        gaps.append(
            EvidenceGap(
                category="metadata",
                description="actively_supported is not specified. This controls CI matrix inclusion.",
                affected_artifact="platformio.ini (custom_meshtastic_actively_supported)",
                required_evidence="Maintainer decision on support status",
                blocking=False,
            )
        )

    if not request.support_level:
        gaps.append(
            EvidenceGap(
                category="metadata",
                description="support_level is not specified (expected: 1 = active, 2 = supported, 3 = extra).",
                affected_artifact="platformio.ini (custom_meshtastic_support_level)",
                required_evidence="Maintainer decision on support tier",
                blocking=False,
            )
        )

    # Revision-scope: multiple display/radio options without disambiguation.
    # Match whole-word choice language rather than raw substrings so normal text
    # like "radio" does not trigger the ambiguity gate.
    note_text = request.board_notes.lower()
    revision_scope_patterns = (
        r"\brevision\b",
        r"\bvariant\b",
        r"\bvariants\b",
        r"\boption\b",
        r"\boptions\b",
        r"\balternative\b",
        r"\balternatives\b",
        r"\bmulti\b",
        r"\btwo\b",
        r"\beither\b",
        r"\bor\b",
    )
    if note_text and any(re.search(pattern, note_text) for pattern in revision_scope_patterns):
        gaps.append(
            EvidenceGap(
                category="revision-scope",
                description=(
                    "Board notes mention multiple variants, revisions, or options. "
                    "The intake must be scoped to a single hardware revision before scaffolding can proceed."
                ),
                affected_artifact="variant.h, platformio.ini",
                required_evidence="Explicit decision on which revision this intake covers",
                blocking=True,
            )
        )

    return gaps


# ---------------------------------------------------------------------------
# T021 – assess_intake
# ---------------------------------------------------------------------------


def assess_intake(request: BoardIntakeRequest, context: dict) -> IntakeAssessment:
    """Produce a full structured assessment from intake request and context."""
    validation_errors = validate_intake(request, context)
    matched = find_matched_patterns(request, context)
    gaps = build_evidence_gaps(request)

    expected_artifacts = [
        f"variants/{request.architecture or '<architecture>'}/<variant-dir>/variant.h",
        f"variants/{request.architecture or '<architecture>'}/<variant-dir>/platformio.ini (env:{request.environment_name or '<env>'})",
    ]
    if request.architecture in {"esp32", "esp32-s3", "esp32-c3", "esp32-c6"}:
        expected_artifacts.append(
            "(optional) variants/.../variant.cpp — only if board requires custom init hooks"
        )
    expected_artifacts += [
        "PlatformIO metadata: all required custom_meshtastic_* keys (see required_metadata below)",
        "(optional) board image under branding/ or images/ if custom_meshtastic_images is set",
    ]

    required_metadata = list(REQUIRED_METADATA_KEYS)
    if request.architecture in BSP_DEFAULT_FAMILIES:
        required_metadata.append(
            f"(BSP note) {request.architecture} boards may inherit some pin defines from BSP headers — "
            "check the Inherited Defaults section of docs/hardware-support-context.md before assuming a missing define is an error."
        )

    risk_flags: list[str] = []
    for err in validation_errors:
        risk_flags.append(f"Validation error: {err}")
    if request.architecture in BSP_DEFAULT_FAMILIES:
        risk_flags.append(
            f"Architecture '{request.architecture}' uses BSP defaults for some pin defines. "
            "Verify which macros are inherited before declaring them explicitly in variant.h."
        )
    if not matched:
        risk_flags.append(
            "No closely matched existing board found for this architecture. "
            "Manual review of variant structure is required."
        )

    next_actions: list[str] = []
    if validation_errors:
        next_actions.append("Resolve validation errors before proceeding.")
    blocking_gaps = [g for g in gaps if g.blocking]
    non_blocking_gaps = [g for g in gaps if not g.blocking]
    for gap in blocking_gaps:
        next_actions.append(
            f"Provide {gap.required_evidence} for {gap.category} ({gap.affected_artifact})."
        )
    if non_blocking_gaps:
        next_actions.append(
            f"Review {len(non_blocking_gaps)} non-blocking gap(s) before merging scaffold output."
        )
    if not next_actions:
        next_actions.append(
            "All required evidence is present. Proceed to scaffold generation."
        )

    scaffold_ready = len(validation_errors) == 0 and all(not g.blocking for g in gaps)

    return IntakeAssessment(
        request=request,
        expected_artifacts=expected_artifacts,
        required_metadata=required_metadata,
        matched_patterns=matched,
        evidence_gaps=gaps,
        risk_flags=risk_flags,
        next_actions=next_actions,
        scaffold_ready=scaffold_ready,
    )


# ---------------------------------------------------------------------------
# T022 – render_assessment_markdown
# ---------------------------------------------------------------------------


def render_assessment_markdown(assessment: IntakeAssessment) -> str:
    req = assessment.request
    lines: list[str] = []

    lines.append(f"# Board Intake Assessment: `{req.environment_name or '(unnamed)'}`")
    lines.append("")
    lines.append(
        f"**Scaffold ready**: {'✅ Yes' if assessment.scaffold_ready else '❌ No — see blocking gaps below'}"
    )
    lines.append("")

    lines.append("## Request Summary")
    lines.append("")
    lines.append(f"- **Environment name**: `{req.environment_name}`")
    lines.append(f"- **Hardware model**: `{req.hardware_model}`")
    lines.append(
        f"- **Hardware model slug**: `{req.hardware_model_slug or '(not set)'}`"
    )
    lines.append(f"- **Display name**: {req.display_name}")
    lines.append(f"- **Architecture**: `{req.architecture}`")
    if req.actively_supported is not None:
        lines.append(f"- **Actively supported**: {req.actively_supported}")
    if req.support_level:
        lines.append(f"- **Support level**: {req.support_level}")
    if req.source_materials:
        lines.append("- **Source materials**:")
        for src in req.source_materials:
            lines.append(f"  - {src}")
    if req.board_notes:
        lines.append(f"- **Board notes**: {req.board_notes}")
    lines.append("")

    lines.append("## Expected Artifacts")
    lines.append("")
    for artifact in assessment.expected_artifacts:
        lines.append(f"- {artifact}")
    lines.append("")

    lines.append("## Required Metadata")
    lines.append("")
    for key in assessment.required_metadata:
        lines.append(
            f"- `{key}`" if key.startswith("custom_meshtastic") else f"- {key}"
        )
    lines.append("")

    lines.append("## Matched Repository Patterns")
    lines.append("")
    if assessment.matched_patterns:
        for pattern in assessment.matched_patterns:
            name = pattern.get("display_name") or pattern.get("environment", "")
            env = pattern.get("environment", "")
            arch = pattern.get("architecture", "")
            vdir = pattern.get("variant_dir", "")
            lines.append(f"- **{name}** (`{env}`, {arch}) — `{vdir}`")
    else:
        lines.append("- No closely matched patterns found for this architecture.")
    lines.append("")

    blocking = [g for g in assessment.evidence_gaps if g.blocking]
    non_blocking = [g for g in assessment.evidence_gaps if not g.blocking]

    lines.append("## Evidence Gaps")
    lines.append("")
    if blocking:
        lines.append("### Blocking")
        lines.append("")
        for gap in blocking:
            lines.append(f"- **[{gap.category}]** {gap.description}")
            lines.append(f"  - Affected: `{gap.affected_artifact}`")
            lines.append(f"  - Required evidence: {gap.required_evidence}")
    else:
        lines.append("*No blocking evidence gaps.*")
    lines.append("")
    if non_blocking:
        lines.append("### Non-blocking (review before merge)")
        lines.append("")
        for gap in non_blocking:
            lines.append(f"- **[{gap.category}]** {gap.description}")
            lines.append(f"  - Affected: `{gap.affected_artifact}`")
            lines.append(f"  - Required evidence: {gap.required_evidence}")
        lines.append("")

    if assessment.risk_flags:
        lines.append("## Risk Flags")
        lines.append("")
        for flag in assessment.risk_flags:
            lines.append(f"- {flag}")
        lines.append("")

    lines.append("## Next Actions")
    lines.append("")
    for i, action in enumerate(assessment.next_actions, 1):
        lines.append(f"{i}. {action}")
    lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# T023 – CLI entry point
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate a board intake request against the repository hardware context."
    )
    parser.add_argument("intake", help="Path to the intake JSON file")
    parser.add_argument(
        "--output",
        default="-",
        help="Output path for the assessment markdown (default: stdout)",
    )
    parser.add_argument(
        "--validate",
        action="store_true",
        help="Exit with code 1 if scaffold_ready is false (useful for CI gate checks)",
    )
    parser.add_argument(
        "--context",
        default=str(DEFAULT_CONTEXT_PATH),
        help="Path to docs/hardware-support-context.md (default: auto-detected)",
    )
    args = parser.parse_args()

    intake_path = Path(args.intake)
    if not intake_path.exists():
        print(f"Error: intake file not found: {intake_path}", file=sys.stderr)
        sys.exit(2)

    request = BoardIntakeRequest.from_json(intake_path)
    context = load_hardware_context(Path(args.context))
    assessment = assess_intake(request, context)
    report = render_assessment_markdown(assessment)

    if args.output == "-":
        print(report)
    else:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(report, encoding="utf-8")
        print(f"Assessment written to {output_path}")

    if args.validate and not assessment.scaffold_ready:
        sys.exit(1)


if __name__ == "__main__":
    main()
