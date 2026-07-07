#!/usr/bin/env bash
# Print (and optionally enforce) the flash/RAM size report for one already-built env.
#
# Usage:
#   bin/check-size.sh <env> [--from-develop] [--from-master] [--merge-base] [--build-local]
#                      [--repo OWNER/REPO] [-- <extra bin/size_report.py args>]
#
# Examples:
#   bin/check-size.sh rak4631
#   bin/check-size.sh rak4631 -- --budgets bin/ram_budgets.json --enforce-budgets
#   bin/check-size.sh rak4631 --from-develop
#   bin/check-size.sh rak4631 --from-develop --merge-base
#   bin/check-size.sh rak4631 --from-develop --merge-base --build-local
#   bin/check-size.sh rak4631 --from-develop --from-master -- --budgets bin/ram_budgets.json
#
# Requires the env to have already been built (manifest is written automatically as
# part of `pio run`, see bin/platformio-custom.py's mtjson target). Does not build for you.
#
# --from-develop / --from-master fetch the same baseline CI itself uses (the newest
# completed run's firmware-sizes-<sha> artifact, see .github/workflows/main_matrix.yml)
# via `gh`, so you don't have to build the base branch locally just to diff against it.
#
# --merge-base changes that to the run for the exact commit where HEAD diverged from
# the branch (git merge-base HEAD <branch>) - the point of the last rebase/merge/branch -
# so the delta reflects only this branch's own commits, not the base branch's drift since.
# Needs a local, up-to-date remote-tracking ref for the branch (meshtastic-upstream/,
# upstream/, or origin/), and can fail if that exact commit's CI run hasn't completed yet.
#
# --build-local skips gh/CI entirely and builds the baseline yourself: it resolves the
# branch (or its merge-base, with --merge-base) to a commit, checks it out into a throwaway
# `git worktree` (so your own working tree/checkout is never touched), builds that one env
# there with `pio run -t mtjson`, and collects its manifest. Slower (a full build of the
# other commit) but works with no network/gh dependency and no stale-artifact risk.

set -e

if [ -z "$1" ]; then
	echo "Usage: $0 <env> [--from-develop] [--from-master] [--merge-base] [--build-local] [--repo OWNER/REPO] [-- <extra bin/size_report.py args>]" >&2
	exit 1
fi
ENV_NAME=$1
shift

FROM_DEVELOP=0
FROM_MASTER=0
MERGE_BASE=0
BUILD_LOCAL=0
REPO=""
while [ $# -gt 0 ]; do
	case "$1" in
	--from-develop)
		FROM_DEVELOP=1
		shift
		;;
	--from-master)
		FROM_MASTER=1
		shift
		;;
	--merge-base)
		MERGE_BASE=1
		shift
		;;
	--build-local)
		BUILD_LOCAL=1
		shift
		;;
	--repo)
		REPO=$2
		shift 2
		;;
	--)
		shift
		break
		;;
	*)
		break
		;;
	esac
done

BUILDDIR=.pio/build/$ENV_NAME
if ! compgen -G "$BUILDDIR"/*.mt.json >/dev/null; then
	echo "Error: no manifest (*.mt.json) found in $BUILDDIR" >&2
	echo "Build the env first, e.g.: pio run -e $ENV_NAME" >&2
	exit 1
fi

# Some setups have no bare `python3` on PATH (e.g. this repo's WSL dev box) - PlatformIO
# always ships its own, so fall back to that rather than failing with "command not found".
PYTHON="$(command -v python3 || command -v python || echo "$HOME/.platformio/penv/bin/python")"
if ! command -v "$PYTHON" >/dev/null 2>&1 && [ ! -x "$PYTHON" ]; then
	echo "Error: no Python interpreter found (tried python3, python, $HOME/.platformio/penv/bin/python)." >&2
	exit 1
fi

TMPDIR=$(mktemp -d)
# Worktrees added under $TMPDIR (see build_baseline_locally) leave admin metadata in
# .git/worktrees even after the directory itself is gone; prune it on the way out.
trap 'rm -rf "$TMPDIR"; git worktree prune >/dev/null 2>&1 || true' EXIT

detect_repo() {
	if [ -n "$REPO" ]; then
		echo "$REPO"
		return 0
	fi
	for remote in meshtastic-upstream upstream origin; do
		url=$(git remote get-url "$remote" 2>/dev/null) || continue
		repo=$(echo "$url" | sed -E 's#.*github\.com[:/]([^/]+/[^/.]+)(\.git)?$#\1#')
		if [ -n "$repo" ]; then
			echo "$repo"
			return 0
		fi
	done
	return 1
}

# Find a local ref for $1 to compute a merge-base against: prefer remote-tracking refs
# (checked in the same priority order as detect_repo, so the two stay consistent), falling
# back to a plain local branch of the same name.
resolve_local_ref() {
	branch=$1
	for remote in meshtastic-upstream upstream origin; do
		if git rev-parse --verify -q "refs/remotes/$remote/$branch" >/dev/null; then
			echo "$remote/$branch"
			return 0
		fi
	done
	if git rev-parse --verify -q "refs/heads/$branch" >/dev/null; then
		echo "$branch"
		return 0
	fi
	return 1
}

# Resolve $1 (optionally its merge-base with HEAD) to a commit, build $ENV_NAME there in a
# throwaway git worktree, and collect its manifest into $2. Never touches the caller's own
# checkout or .pio/build - the worktree and its build output live entirely under $TMPDIR.
build_baseline_locally() {
	branch=$1
	outfile=$2

	ref=$(resolve_local_ref "$branch") || {
		echo "Error: no local ref for '$branch' to build from (tried meshtastic-upstream/$branch, upstream/$branch, origin/$branch, $branch)." >&2
		echo "Fetch it first, e.g.: git fetch meshtastic-upstream $branch" >&2
		exit 1
	}
	case "$ref" in
	meshtastic-upstream/* | upstream/* | origin/*)
		git fetch --quiet "${ref%%/*}" "$branch" 2>/dev/null || true
		;;
	esac

	if [ "$MERGE_BASE" = 1 ]; then
		sha=$(git merge-base HEAD "$ref") || {
			echo "Error: 'git merge-base HEAD $ref' failed - history may be too shallow to find a common ancestor." >&2
			echo "Try: git fetch --unshallow" >&2
			exit 1
		}
	else
		sha=$(git rev-parse "$ref")
	fi

	PIO="$(command -v pio || command -v platformio || echo "$HOME/.platformio/penv/bin/pio")"
	if ! command -v "$PIO" >/dev/null 2>&1 && [ ! -x "$PIO" ]; then
		echo "Error: --build-local needs the 'pio'/'platformio' CLI, which isn't on PATH or at $PIO." >&2
		exit 1
	fi

	worktree="$TMPDIR/worktree-$branch"
	echo "Building '$branch' ($ref) locally at $sha for $ENV_NAME (this runs a full pio build) ..." >&2
	if ! git worktree add --quiet --detach "$worktree" "$sha"; then
		echo "Error: 'git worktree add' failed for $sha ($branch). It may already be checked out elsewhere." >&2
		exit 1
	fi

	if ! "$PIO" run -d "$worktree" -e "$ENV_NAME" -t mtjson; then
		echo "Error: building '$ENV_NAME' at $sha ($branch) failed - see build output above." >&2
		exit 1
	fi

	"$PYTHON" bin/collect_sizes.py "$worktree/.pio/build/$ENV_NAME" "$outfile"
	echo "Baseline '$branch' = $sha (built locally)" >&2
}

# Fetch the newest completed CI run on $1 that still has a firmware-sizes artifact,
# writing its current-sizes.json to $2. Mirrors the logic in
# .github/workflows/main_matrix.yml so local and CI baselines agree.
fetch_baseline() {
	branch=$1
	outfile=$2

	if [ "$BUILD_LOCAL" = 1 ]; then
		build_baseline_locally "$branch" "$outfile"
		return
	fi

	if ! command -v gh >/dev/null 2>&1; then
		echo "Error: --from-$branch needs the GitHub CLI ('gh'), which isn't installed." >&2
		echo "Install it: https://cli.github.com/, then run 'gh auth login'." >&2
		echo "Or skip gh entirely: add --build-local to build '$branch' yourself in a throwaway worktree." >&2
		exit 1
	fi
	if ! gh auth status >/dev/null 2>&1; then
		echo "Error: --from-$branch needs 'gh' to be authenticated. Run: gh auth login" >&2
		exit 1
	fi

	repo=$(detect_repo) || {
		echo "Error: could not determine which GitHub repo to query (no meshtastic-upstream/upstream/origin remote points at github.com)." >&2
		echo "Pass --repo OWNER/REPO explicitly." >&2
		exit 1
	}

	if [ "$MERGE_BASE" = 1 ]; then
		ref=$(resolve_local_ref "$branch") || {
			echo "Error: no local ref for '$branch' to compute a merge-base from (tried meshtastic-upstream/$branch, upstream/$branch, origin/$branch, $branch)." >&2
			echo "Fetch it first, e.g.: git fetch meshtastic-upstream $branch" >&2
			exit 1
		}
		case "$ref" in
		meshtastic-upstream/* | upstream/* | origin/*)
			git fetch --quiet "${ref%%/*}" "$branch" 2>/dev/null || true
			;;
		esac
		sha=$(git merge-base HEAD "$ref") || {
			echo "Error: 'git merge-base HEAD $ref' failed - history may be too shallow to find a common ancestor." >&2
			echo "Try: git fetch --unshallow" >&2
			exit 1
		}
		echo "Merge-base of HEAD and $branch ($ref) = $sha" >&2

		found=0
		run_ids=$(gh api "repos/$repo/actions/runs?head_sha=$sha" \
			--jq '.workflow_runs[] | select(.name=="CI" and .status=="completed") | .id')
		for rid in $run_ids; do
			name=$(gh api "repos/$repo/actions/runs/$rid/artifacts" \
				--jq '.artifacts[] | select(.name | startswith("firmware-sizes-")) | select(.expired == false) | .name' | head -1)
			[ -z "$name" ] && continue
			if gh run download "$rid" -R "$repo" --name "$name" --dir "$TMPDIR/baseline-$branch" >/dev/null; then
				cp "$TMPDIR/baseline-$branch/current-sizes.json" "$outfile"
				echo "Baseline '$branch' = merge-base $sha (run $rid) from $repo" >&2
				found=1
				break
			fi
		done

		if [ "$found" -ne 1 ]; then
			echo "Error: no completed CI run with a firmware-sizes artifact for merge-base commit $sha ($branch in $repo)." >&2
			echo "Either that commit's CI run hasn't finished yet (try again shortly), or it predates the" >&2
			echo "firmware-sizes artifact / never ran in this repo." >&2
			echo "" >&2
			echo "To bring it in, either:" >&2
			echo "  - wait for CI to finish on that commit and try again, or" >&2
			echo "  - fall back to --from-$branch (newest available baseline, not pinned to the merge-base), or" >&2
			echo "  - add --build-local to build $sha yourself in a throwaway worktree." >&2
			exit 1
		fi
		return
	fi

	runs=$(gh run list -R "$repo" --workflow CI --branch "$branch" --status completed \
		--limit 20 --json databaseId,headSha,createdAt \
		--jq '.[] | "\(.databaseId) \(.headSha) \(.createdAt)"')

	found=0
	while read -r rid sha created; do
		[ -z "$rid" ] && continue
		name=$(gh api "repos/$repo/actions/runs/$rid/artifacts" \
			--jq '.artifacts[] | select(.name | startswith("firmware-sizes-")) | select(.expired == false) | .name' | head -1)
		[ -z "$name" ] && continue
		if gh run download "$rid" -R "$repo" --name "$name" --dir "$TMPDIR/baseline-$branch" >/dev/null; then
			cp "$TMPDIR/baseline-$branch/current-sizes.json" "$outfile"
			echo "Baseline '$branch' = $sha ($created) from $repo" >&2
			found=1
			break
		fi
	done <<<"$runs"

	if [ "$found" -ne 1 ]; then
		# Nothing to download isn't necessarily a bug in this script: it means the size-report
		# job in .github/workflows/main_matrix.yml has never completed on this branch in this
		# repo (new fork, workflow just added, or artifacts aged out of the retention window).
		# That job only runs as part of CI on pushes to $branch - it is not triggered by a local
		# build or checkout, so point at the action that actually produces the artifact.
		echo "Error: no completed CI run on '$branch' in $repo has a firmware-sizes artifact." >&2
		echo "That artifact is produced by the size-report job in .github/workflows/main_matrix.yml" >&2
		echo "on every CI run for $branch - it does not run as part of a local build or git checkout." >&2
		echo "" >&2
		echo "To bring it in, either:" >&2
		echo "  - push to $branch (or open/update a PR against it) and let CI run once, or" >&2
		echo "  - add --build-local to build '$branch' yourself in a throwaway worktree." >&2
		exit 1
	fi
}

BASELINE_ARGS=()
if [ "$FROM_DEVELOP" = 1 ]; then
	fetch_baseline develop "$TMPDIR/develop-sizes.json"
	BASELINE_ARGS+=(--baseline "develop:$TMPDIR/develop-sizes.json")
fi
if [ "$FROM_MASTER" = 1 ]; then
	fetch_baseline master "$TMPDIR/master-sizes.json"
	BASELINE_ARGS+=(--baseline "master:$TMPDIR/master-sizes.json")
fi

# check-size.sh only ever measures one env, but bin/ram_budgets.json covers every budgeted
# env across the whole build matrix (that's the right shape for the CI report, which sees
# them all). Passed through unfiltered, size_report.py's budget table would list every other
# budgeted env as a spurious "n/a" row next to the one env we actually built. Rewrite any
# --budgets PATH in the passthrough args to a copy containing only this env's entry.
FINAL_ARGS=()
while [ $# -gt 0 ]; do
	case "$1" in
	--budgets)
		filtered="$TMPDIR/budgets-filtered.json"
		"$PYTHON" -c '
import json, sys
with open(sys.argv[1]) as f:
    data = json.load(f)
with open(sys.argv[3], "w") as f:
    json.dump({k: v for k, v in data.items() if k == sys.argv[2]}, f)
' "$2" "$ENV_NAME" "$filtered"
		FINAL_ARGS+=(--budgets "$filtered")
		shift 2
		;;
	*)
		FINAL_ARGS+=("$1")
		shift
		;;
	esac
done

"$PYTHON" bin/collect_sizes.py "$BUILDDIR" "$TMPDIR/current-sizes.json"
# Default to the terminal-friendly bar format (like PlatformIO's own post-build RAM:/Flash:
# summary) rather than the markdown this shares with the CI PR-comment path; a later
# --format in FINAL_ARGS (passed after --) still wins, since argparse takes the last value.
"$PYTHON" bin/size_report.py "$TMPDIR/current-sizes.json" --format text "${BASELINE_ARGS[@]}" "${FINAL_ARGS[@]}"
