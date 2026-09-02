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
set -o pipefail

if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ] || [ -z "$4" ]; then
	echo "Usage: $0 <repo> <branch> <outfile> <download-dir>" >&2
	exit 1
fi
REPO=$1
BRANCH=$2
OUTFILE=$3
DOWNLOAD_DIR=$4

# Some setups have no bare `python3` on PATH (e.g. this repo's WSL dev box) - PlatformIO
# always ships its own, so fall back to that rather than failing with "command not found".
PYTHON="$(command -v python3 || command -v python || echo "$HOME/.platformio/penv/bin/python")"

runs=$(gh run list -R "$REPO" --workflow CI --branch "$BRANCH" --status completed \
	--limit 20 --json databaseId,headSha,createdAt \
	--jq '.[] | "\(.databaseId) \(.headSha) \(.createdAt)"')

while read -r rid sha created; do
	[ -z "$rid" ] && continue
	artifact_id=$(gh api "repos/$REPO/actions/runs/$rid/artifacts" \
		--jq '[.artifacts[] | select(.name | startswith("firmware-sizes-")) | select(.expired == false)] | sort_by(.created_at) | last | .id')
	[ -z "$artifact_id" ] && continue
	mkdir -p "$DOWNLOAD_DIR"
	# Download by artifact ID via the API rather than `gh run download <rid> --name <name>`,
	# which is known to fetch an earlier attempt's artifact instead of the latest one when a
	# run has been re-run (cli/cli#12437) - exactly the "CI was re-run after being red" case
	# this baseline scan exists to handle.
	if gh api "repos/$REPO/actions/artifacts/$artifact_id/zip" >"$DOWNLOAD_DIR/artifact.zip" &&
		"$PYTHON" -c "import zipfile, sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" \
			"$DOWNLOAD_DIR/artifact.zip" "$DOWNLOAD_DIR"; then
		cp "$DOWNLOAD_DIR/current-sizes.json" "$OUTFILE"
		echo "$sha $created"
		exit 0
	fi
done <<<"$runs"

exit 1
