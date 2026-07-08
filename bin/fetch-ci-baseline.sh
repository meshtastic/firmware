#!/usr/bin/env bash
# Download the newest completed CI run's firmware-sizes-<sha> artifact for a branch.
#
# Usage: bin/fetch-ci-baseline.sh <repo> <branch> <outfile> <download-dir>
#
# Scans up to 20 recent completed CI runs on <branch> in <repo> (newest first) and downloads
# the first one that still has a non-expired firmware-sizes-* artifact, copying its
# current-sizes.json to <outfile>. Deliberately does not require conclusion=success: the
# firmware-sizes artifact is uploaded independently of the rest of CI, so demanding an
# all-green run would pin the baseline to the last fully-green build and let it go stale
# whenever the branch is red for a while.
#
# On success, prints "<sha> <created>" to stdout and exits 0. On failure, exits 1 with
# nothing on stdout - the caller is expected to print its own contextual error message.
#
# Shared by the develop/master baseline steps in .github/workflows/main_matrix.yml and by
# check-size.sh's fetch_baseline() (non-merge-base path), so the artifact-selection logic
# only has one implementation to keep in sync.

set -e

REPO=$1
BRANCH=$2
OUTFILE=$3
DOWNLOAD_DIR=$4

runs=$(gh run list -R "$REPO" --workflow CI --branch "$BRANCH" --status completed \
	--limit 20 --json databaseId,headSha,createdAt \
	--jq '.[] | "\(.databaseId) \(.headSha) \(.createdAt)"')

while read -r rid sha created; do
	[ -z "$rid" ] && continue
	name=$(gh api "repos/$REPO/actions/runs/$rid/artifacts" \
		--jq '.artifacts[] | select(.name | startswith("firmware-sizes-")) | select(.expired == false) | .name' | head -1)
	[ -z "$name" ] && continue
	if gh run download "$rid" -R "$REPO" --name "$name" --dir "$DOWNLOAD_DIR" >/dev/null; then
		cp "$DOWNLOAD_DIR/current-sizes.json" "$OUTFILE"
		echo "$sha $created"
		exit 0
	fi
done <<<"$runs"

exit 1
