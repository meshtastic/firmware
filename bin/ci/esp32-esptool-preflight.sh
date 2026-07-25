#!/usr/bin/env bash
#
# Defuse pioarduino's esptool install on esp32 builds.
#
# The espressif32 platform (pinned to 55.03.39 in variants/esp32/esp32-common.ini)
# guards its esptool install with a probe that only passes when `import esptool`
# resolves from inside the tool-esptoolpy package dir. Nothing ever satisfies that
# probe on a first pass -- the editable install is never performed at container-build
# time, and python_deps carries no esptool -- so every esp32 build falls through to:
#
#   uv pip install --quiet --force-reinstall --python=<penv> -e <tool-esptoolpy>
#
# under a hard-coded timeout=60 (builder/penv_setup.py:811-814). `--force-reinstall`
# is an alias of uv's `--reinstall`, which implies `--refresh`, so that call re-resolves
# esptool and its dependencies against PyPI instead of being served from cache. It is a
# latency coin-flip against a 60s budget, and it loses whenever the runners are busy.
#
# When it loses, subprocess.TimeoutExpired is not caught (only CalledProcessError is,
# at penv_setup.py:817), so _setup_python_environment_core never reaches its
# `return penv_python, esptool_binary_path`. platform.py swallows the exception, and
# setup_python_env then returns None implicitly, which surfaces as the real-world
# symptom:
#
#   TypeError: cannot unpack non-iterable NoneType object   (builder/main.py:52)
#
# This script runs inside the build container -- gh-action-firmware bind-mounts the
# workspace and its entrypoint invokes bin/build-esp32.sh -- so it takes effect on the
# next job, against any container image vintage.
#
# It is deliberately fail-open. Part A alone removes the hard failure, so nothing here
# is worth reddening a build over: an unrecognized upstream layout warns and continues.

set -uo pipefail

core="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
penv="$core/penv"
pkg="$core/packages/tool-esptoolpy"

# --- Part A: de-fang the install command itself -------------------------------------
#
# Drop whichever reinstall flag the pinned platform uses so the install is cache-served
# rather than re-fetched, and raise the 60s cap to 600s so residual slowness becomes a
# slow success instead of a build failure. Two spellings are handled:
#
#   "--force-reinstall"                  -- pioarduino 55.03.39 and earlier
#   "--reinstall-package", "esptool"     -- meshtastic fork, cfdf56e onwards
#
# Both imply a uv refresh (`--reinstall` => `--refresh`, `--reinstall-package` =>
# `--refresh-package`), so either one forces a network fetch on every build.
#
# Both call sites are patched. Only the second is reachable from the minimal
# (env is None) path a CI build takes, but this is vendored third-party code and a
# global edit is cheaper to reason about than a targeted one. The
# `], timeout=60, env=uv_env)` anchor is specific enough to leave the unrelated 60s
# timeouts elsewhere in the file untouched.
patched=0
while IFS= read -r f; do
	tmp="$f.preflight.tmp"
	sed -e 's/ "--force-reinstall",//g' \
		-e 's/^[[:space:]]*"--reinstall-package",[[:space:]]*"esptool",[[:space:]]*$//' \
		-e 's/ "--reinstall-package", "esptool",//g' \
		-e 's/], timeout=60, env=uv_env)/], timeout=600, env=uv_env)/g' \
		"$f" >"$tmp" && mv -f "$tmp" "$f" || {
		rm -f "$tmp"
		continue
	}

	# Drop bytecode compiled from the pre-patch source so the edit actually takes effect.
	rm -f "$(dirname "$f")/__pycache__/penv_setup."*.pyc

	# The hazard is a reinstall flag still paired with the 60s cap. Warn only when both
	# survive, so this stays quiet once the fix lands upstream and the patterns stop
	# matching because there is nothing left to patch.
	if grep -qE -- '--force-reinstall|--reinstall-package' "$f" &&
		grep -q 'timeout=60, env=uv_env)' "$f"; then
		echo "preflight: WARNING $f still reinstalls under a 60s cap (upstream layout changed?)" >&2
	fi
	echo "preflight: patched $f"
	patched=$((patched + 1))
done < <(find "$core/platforms" -path '*/builder/penv_setup.py' -type f 2>/dev/null)

if [ "$patched" -eq 0 ]; then
	echo "preflight: no builder/penv_setup.py under $core/platforms - nothing to patch" >&2
fi

# --- Part B: pre-satisfy the probe so the install is skipped entirely ---------------
#
# Best-effort only. tool-esptoolpy is not reliably materialized as a Python project this
# early in the build, which is why this checks for packaging metadata rather than just a
# directory. If it is not ready, Part A has already made the platform's own install safe.
if [ -x "$penv/bin/uv" ] && { [ -f "$pkg/pyproject.toml" ] || [ -f "$pkg/setup.py" ]; }; then
	export UV_CACHE_DIR="${UV_CACHE_DIR:-$core/.cache/uv}"

	# Replicates the probe at penv_setup.py:785-804 exactly (realpath + normcase + startswith).
	probe() {
		"$penv/bin/python" - "$pkg" <<'PY'
import esptool, os, sys
expected = os.path.normcase(os.path.realpath(sys.argv[1]))
actual = os.path.normcase(os.path.realpath(os.path.dirname(esptool.__file__)))
sys.exit(0 if actual.startswith(expected) else 1)
PY
	}

	if probe; then
		echo "preflight: esptool already resolves under $pkg (probe will MATCH)"
	else
		# A regular esptool ahead of the tool package on sys.path shadows it and makes the
		# probe miss, so drop it before installing the tool package editable.
		"$penv/bin/uv" pip uninstall --python "$penv/bin/python" esptool >/dev/null 2>&1
		if "$penv/bin/uv" pip install --python "$penv/bin/python" -e "$pkg" && probe; then
			echo "preflight: esptool baked editable from $pkg"
		else
			echo "preflight: could not bake esptool; relying on the patched install path" >&2
		fi
	fi
else
	echo "preflight: penv or tool-esptoolpy not ready - relying on the patched install path" >&2
fi

exit 0
