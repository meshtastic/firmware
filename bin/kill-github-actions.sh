#!/bin/bash

# Script to cancel all running GitHub Actions workflows
# Requires GitHub CLI (gh) to be installed and authenticated

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if gh CLI is installed
if ! command -v gh &> /dev/null; then
    print_error "GitHub CLI (gh) is not installed. Please install it first:"
    echo "  brew install gh"
    echo "  Or visit: https://cli.github.com/"
    exit 1
fi

# Check if authenticated
if ! gh auth status &> /dev/null; then
    print_error "GitHub CLI is not authenticated. Please run:"
    echo "  gh auth login"
    exit 1
fi

# Get repository info
REPO=$(gh repo view --json owner,name -q '.owner.login + "/" + .name')
if [[ -z "$REPO" ]]; then
    print_error "Could not determine repository. Make sure you're in a GitHub repository."
    exit 1
fi

print_status "Working with repository: $REPO"

# Get all active workflows (both queued and in-progress)
print_status "Fetching active workflows (queued and in-progress)..."
QUEUED_WORKFLOWS=$(gh run list --status queued --json databaseId,displayTitle,headBranch,status --limit 100)
IN_PROGRESS_WORKFLOWS=$(gh run list --status in_progress --json databaseId,displayTitle,headBranch,status --limit 100)

# Combine both lists
ALL_WORKFLOWS=$(echo "$QUEUED_WORKFLOWS $IN_PROGRESS_WORKFLOWS" | jq -s 'add | unique_by(.databaseId)')

if [[ "$ALL_WORKFLOWS" == "[]" ]]; then
    print_status "No active workflows found."
    exit 0
fi

# Parse and display active workflows
echo
print_warning "Found active workflows:"
echo "$ALL_WORKFLOWS" | jq -r '.[] | "  - \(.displayTitle) (Branch: \(.headBranch), Status: \(.status), ID: \(.databaseId))"'

echo
read -p "Do you want to cancel ALL these workflows? (y/N): " -n 1 -r
echo

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    print_status "Cancelled by user."
    exit 0
fi

# Cancel each workflow
print_status "Cancelling workflows..."
CANCELLED_COUNT=0
FAILED_COUNT=0

while IFS= read -r WORKFLOW_ID; do
    if [[ -n "$WORKFLOW_ID" ]]; then
        print_status "Cancelling workflow ID: $WORKFLOW_ID"
        if gh run cancel "$WORKFLOW_ID" 2>/dev/null; then
            ((CANCELLED_COUNT++))
        else
            print_error "Failed to cancel workflow ID: $WORKFLOW_ID"
            ((FAILED_COUNT++))
        fi
    fi
done < <(echo "$ALL_WORKFLOWS" | jq -r '.[].databaseId')

echo
print_status "Summary:"
echo "  - Cancelled: $CANCELLED_COUNT workflows"
if [[ $FAILED_COUNT -gt 0 ]]; then
    echo "  - Failed: $FAILED_COUNT workflows"
fi

print_status "Done!"

# Optional: Show remaining active workflows
echo
print_status "Checking for any remaining active workflows..."
REMAINING_QUEUED=$(gh run list --status queued --json databaseId --limit 10)
REMAINING_IN_PROGRESS=$(gh run list --status in_progress --json databaseId --limit 10)
REMAINING_ALL=$(echo "$REMAINING_QUEUED $REMAINING_IN_PROGRESS" | jq -s 'add | unique_by(.databaseId)')

if [[ "$REMAINING_ALL" == "[]" ]]; then
    print_status "All workflows successfully cancelled."
else
    REMAINING_COUNT=$(echo "$REMAINING_ALL" | jq '. | length')
    print_warning "Still $REMAINING_COUNT workflows active (may take a moment to update status)"
fi