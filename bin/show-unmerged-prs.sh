#!/bin/bash

# Script to show commits in develop that are not in master
# with their associated PR info and commit hashes
# 
# Usage:
#   ./show-unmerged-prs.sh            # Show all unmerged commits
#   ./show-unmerged-prs.sh --bugfix   # Show only bugfix-labeled PRs

set -e

REPO="firmware"
OWNER="meshtastic"
BASE_BRANCH="master"
HEAD_BRANCH="develop"
LIMIT=100
FILTER_LABEL=""

# Parse arguments
for arg in "$@"; do
    case $arg in
        --bugfix)
            FILTER_LABEL="bugfix"
            shift
            ;;
        --feature)
            FILTER_LABEL="feature"
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  --bugfix     Show only PRs labeled with 'bugfix'"
            echo "  --feature    Show only PRs labeled with 'feature'"
            echo "  --help       Show this help message"
            exit 0
            ;;
    esac
done

if [ -n "$FILTER_LABEL" ]; then
    echo "Fetching commits in $HEAD_BRANCH that are not in $BASE_BRANCH (filtered by label: $FILTER_LABEL)..."
else
    echo "Fetching commits in $HEAD_BRANCH that are not in $BASE_BRANCH..."
fi
echo ""

# Check if gh CLI is available
if ! command -v gh &> /dev/null; then
    echo "ERROR: GitHub CLI (gh) not found. Please install it first."
    echo "Visit: https://cli.github.com/"
    exit 1
fi

# Get commits in develop that are not in master
# For each commit, try to find associated PR
git fetch origin develop master 2>/dev/null || true

# Use git to get the list of commits
commits=$(git log --pretty=format:"%H|%s" origin/master..origin/develop | head -n $LIMIT)

count=0
displayed=0
echo "Commits in $HEAD_BRANCH not in $BASE_BRANCH:"
echo "=============================================="
echo ""

while IFS='|' read -r hash subject; do
    ((count++))
    
    # Try to find the PR for this commit
    # Extract PR number, title, description, and labels
    pr_response=$(gh api -X GET "/repos/$OWNER/$REPO/commits/$hash/pulls" \
        -H "Accept: application/vnd.github.v3+json" 2>/dev/null | \
        jq -r '.[0] | "\(.number)|\(.title)|\(.body // "No description")|\(.labels | map(.name) | join(","))"' 2>/dev/null || echo "||||")
    
    if [ -z "$pr_response" ] || [ "$pr_response" = "||||" ]; then
        # If no PR found, skip if filter is active, otherwise show the commit
        if [ -z "$FILTER_LABEL" ]; then
            ((displayed++))
            echo "[$displayed] Commit: $hash"
            echo "    Subject: $subject"
            echo "    PR: Not found in GitHub"
            echo ""
        fi
    else
        IFS='|' read -r pr_num pr_title pr_desc pr_labels <<< "$pr_response"
        
        # Check if filter matches
        if [ -n "$FILTER_LABEL" ]; then
            # Only show if the label is in the labels list
            if ! echo "$pr_labels" | grep -q "$FILTER_LABEL"; then
                continue
            fi
        fi
        
        ((displayed++))
        echo "[$displayed] PR #$pr_num - $pr_title"
        echo "    Commit: $hash"
        if [ -n "$pr_desc" ] && [ "$pr_desc" != "No description" ]; then
            # Truncate description to 200 chars
            desc_short="${pr_desc:0:200}"
            [ ${#pr_desc} -gt 200 ] && desc_short+="..."
            echo "    Description: $desc_short"
        fi
        if [ -n "$pr_labels" ] && [ "$pr_labels" != "" ]; then
            echo "    Labels: $pr_labels"
        fi
        echo ""
    fi
done <<< "$commits"

echo ""
if [ -n "$FILTER_LABEL" ]; then
    echo "Done. Showing $displayed PRs with label '$FILTER_LABEL' from $displayed commits checked."
else
    echo "Done. Showing $displayed commits from $HEAD_BRANCH not in $BASE_BRANCH."
fi
