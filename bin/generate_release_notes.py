#!/usr/bin/env python3
"""
Generate release notes from merged PRs on develop and master branches.
Categorizes PRs into Enhancements and Bug Fixes/Maintenance sections.
"""

import subprocess
import re
import json
import sys
from datetime import datetime


def get_last_release_tag():
    """Get the most recent release tag."""
    result = subprocess.run(
        ["git", "describe", "--tags", "--abbrev=0"],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def get_tag_date(tag):
    """Get the commit date (ISO 8601) of the tag."""
    result = subprocess.run(
        ["git", "show", "-s", "--format=%cI", tag],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def get_merged_prs_since_tag(tag, branch):
    """Get all merged PRs since the given tag on the specified branch."""
    # Get commits since tag on the branch - look for PR numbers in parentheses
    result = subprocess.run(
        [
            "git",
            "log",
            f"{tag}..origin/{branch}",
            "--oneline",
        ],
        capture_output=True,
        text=True,
    )

    prs = []
    seen_pr_numbers = set()

    for line in result.stdout.strip().split("\n"):
        if not line:
            continue

        # Extract PR number from commit message - format: "Title (#1234)"
        pr_match = re.search(r"\(#(\d+)\)", line)
        if pr_match:
            pr_number = pr_match.group(1)
            if pr_number not in seen_pr_numbers:
                seen_pr_numbers.add(pr_number)
                prs.append(pr_number)

    return prs


def get_pr_details(pr_number):
    """Get PR details from GitHub API via gh CLI."""
    try:
        result = subprocess.run(
            [
                "gh",
                "pr",
                "view",
                pr_number,
                "--json",
                "title,author,labels,url",
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        return json.loads(result.stdout)
    except subprocess.CalledProcessError:
        return None


def should_exclude_pr(pr_details):
    """Check if PR should be excluded from release notes."""
    if not pr_details:
        return True

    title = pr_details.get("title", "").lower()

    # Exclude trunk update PRs
    if "upgrade trunk" in title or "update trunk" in title or "trunk update" in title:
        return True

    # Exclude protobuf update PRs
    if "update protobufs" in title or "update protobuf" in title:
        return True

    # Exclude automated version bump PRs
    if "bump release version" in title or "bump version" in title:
        return True

    return False


def is_dependency_update(pr_details):
    """Check if PR is a dependency/chore update."""
    if not pr_details:
        return False

    title = pr_details.get("title", "").lower()
    author = pr_details.get("author", {}).get("login", "").lower()
    labels = [label.get("name", "").lower() for label in pr_details.get("labels", [])]

    # Check for renovate or dependabot authors
    if "renovate" in author or "dependabot" in author:
        return True

    # Check for chore(deps) pattern
    if re.match(r"^chore\(deps\):", title):
        return True

    # Check for digest update patterns
    if re.match(r".*digest to [a-f0-9]+", title, re.IGNORECASE):
        return True

    # Check for dependency-related labels
    dependency_labels = ["dependencies", "deps", "renovate"]
    if any(dep in label for label in labels for dep in dependency_labels):
        return True

    return False


def is_enhancement(pr_details):
    """Determine if PR is an enhancement based on labels and title."""
    labels = [label.get("name", "").lower() for label in pr_details.get("labels", [])]

    # Check labels first
    enhancement_labels = ["enhancement", "feature", "feat", "new feature"]
    for label in labels:
        if any(enh in label for enh in enhancement_labels):
            return True

    # Check title prefixes
    title = pr_details.get("title", "")
    enhancement_prefixes = ["feat:", "feature:", "add:"]
    title_lower = title.lower()
    for prefix in enhancement_prefixes:
        if title_lower.startswith(prefix) or f" {prefix}" in title_lower:
            return True

    return False


def clean_title(title):
    """Clean up PR title for release notes."""
    # Remove common prefixes
    prefixes_to_remove = [
        r"^fix:\s*",
        r"^feat:\s*",
        r"^feature:\s*",
        r"^bug:\s*",
        r"^bugfix:\s*",
        r"^chore:\s*",
        r"^chore\([^)]+\):\s*",
        r"^refactor:\s*",
        r"^docs:\s*",
        r"^ci:\s*",
        r"^build:\s*",
        r"^perf:\s*",
        r"^style:\s*",
        r"^test:\s*",
    ]

    cleaned = title
    for prefix in prefixes_to_remove:
        cleaned = re.sub(prefix, "", cleaned, flags=re.IGNORECASE)

    # Ensure first letter is capitalized
    if cleaned:
        cleaned = cleaned[0].upper() + cleaned[1:]

    return cleaned.strip()


def format_pr_line(pr_details):
    """Format a PR as a markdown bullet point."""
    title = clean_title(pr_details.get("title", "Unknown"))
    author = pr_details.get("author", {}).get("login", "unknown")
    url = pr_details.get("url", "")

    return f"- {title} by @{author} in {url}"


def get_new_contributors(pr_details_list, tag, repo="meshtastic/firmware"):
    """Find contributors who made their first merged PR before this release.

    GitHub usernames do not necessarily match git commit authors, so we use the
    GitHub search API via `gh` to see if the user has any merged PRs before the
    tag date. This mirrors how GitHub's "Generate release notes" feature works.
    """

    bot_authors = {"github-actions", "renovate", "dependabot", "app/renovate", "app/github-actions", "app/dependabot"}

    new_contributors = []
    seen_authors = set()

    try:
        tag_date = get_tag_date(tag)
    except subprocess.CalledProcessError:
        print(f"Warning: Could not determine tag date for {tag}; skipping new contributor detection", file=sys.stderr)
        return []

    for pr in pr_details_list:
        author = pr.get("author", {}).get("login", "")
        if not author or author in seen_authors:
            continue

        # Skip bots
        if author.lower() in bot_authors or author.startswith("app/"):
            continue

        seen_authors.add(author)

        try:
            # Search for merged PRs by this author created before the tag date
            search_query = f"is:pr author:{author} repo:{repo} closed:<=\"{tag_date}\""
            search = subprocess.run(
                [
                    "gh",
                    "search",
                    "issues",
                    "--json",
                    "number,mergedAt,createdAt",
                    "--state",
                    "closed",
                    "--limit",
                    "200",
                    search_query,
                ],
                capture_output=True,
                text=True,
            )

            if search.returncode != 0:
                # If gh fails, be conservative and skip adding to new contributors
                print(f"Warning: gh search failed for author {author}: {search.stderr.strip()}", file=sys.stderr)
                continue

            results = json.loads(search.stdout or "[]")
            # If any merged PR exists before or on tag date, not a new contributor
            had_prior_pr = any(item.get("mergedAt") for item in results)

            if not had_prior_pr:
                new_contributors.append((author, pr.get("url", "")))

        except Exception as e:
            print(f"Warning: Could not check contributor history for {author}: {e}", file=sys.stderr)
            continue

    return new_contributors


def main():
    if len(sys.argv) < 2:
        print("Usage: generate_release_notes.py <new_version>", file=sys.stderr)
        sys.exit(1)

    new_version = sys.argv[1]

    # Get last release tag
    try:
        last_tag = get_last_release_tag()
    except subprocess.CalledProcessError:
        print("Error: Could not find last release tag", file=sys.stderr)
        sys.exit(1)

    # Collect PRs from both branches
    all_pr_numbers = set()

    for branch in ["develop", "master"]:
        try:
            prs = get_merged_prs_since_tag(last_tag, branch)
            all_pr_numbers.update(prs)
        except Exception as e:
            print(f"Warning: Could not get PRs from {branch}: {e}", file=sys.stderr)

    # Get details for all PRs
    enhancements = []
    bug_fixes = []
    dependencies = []
    all_pr_details = []

    for pr_number in sorted(all_pr_numbers, key=int):
        details = get_pr_details(pr_number)
        if details and not should_exclude_pr(details):
            all_pr_details.append(details)
            if is_dependency_update(details):
                dependencies.append(details)
            elif is_enhancement(details):
                enhancements.append(details)
            else:
                bug_fixes.append(details)

    # Generate release notes
    output = []

    if enhancements:
        output.append("## 🚀 Enhancements\n")
        for pr in enhancements:
            output.append(format_pr_line(pr))
        output.append("")

    if bug_fixes:
        output.append("## 🐛 Bug fixes and maintenance\n")
        for pr in bug_fixes:
            output.append(format_pr_line(pr))
        output.append("")

    if dependencies:
        output.append("## ⚙️ Dependencies\n")
        for pr in dependencies:
            output.append(format_pr_line(pr))
        output.append("")

    # Find new contributors (GitHub-accurate check using merged PRs before tag date)
    new_contributors = get_new_contributors(all_pr_details, last_tag)
    if new_contributors:
        output.append("## New Contributors\n")
        for author, url in new_contributors:
            # Find first PR URL for this contributor
            first_pr_url = url
            for pr in all_pr_details:
                if pr.get("author", {}).get("login") == author:
                    first_pr_url = pr.get("url", url)
                    break
            output.append(f"- @{author} made their first contribution in {first_pr_url}")
        output.append("")

    # Add full changelog link
    output.append(
        f"**Full Changelog**: https://github.com/meshtastic/firmware/compare/{last_tag}...v{new_version}"
    )

    print("\n".join(output))


if __name__ == "__main__":
    main()
