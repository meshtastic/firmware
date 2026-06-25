#
# Auto-locate the Emscripten SDK for [env:native-wasm].
#
# The platform-wasm builder needs `emcc` on PATH (or $EMSDK set). When `pio run
# -e native-wasm` is invoked from a shell that didn't source `emsdk_env.sh` — a
# VS Code task, a bare terminal, an IDE build button — the build fails at emcc
# detection. This pre-script probes the usual emsdk locations (mirroring the
# retired bin/build-portduino-wasm.sh), sources `emsdk_env.sh`, and imports the
# resulting environment so the platform builder and emcc itself see PATH /
# EMSDK / EM_CONFIG / etc. It runs before the platform builder.
#
# No-op when emcc is already reachable (CI sources emsdk first; so does anyone
# who set it up in their shell), and silent-but-harmless if no SDK is found —
# the platform builder then emits its own "install the Emscripten SDK" error.
#
import os
import shutil
import subprocess

Import("env")


def _import_emsdk_environment():
    if env["PIOENV"] != "native-wasm":
        return
    if shutil.which("emcc"):
        return  # already usable (CI, or the user sourced emsdk)

    project_dir = env.subst("$PROJECT_DIR")
    home = os.path.expanduser("~")
    # Priority order, same spirit as the old standalone build script.
    candidates = [
        os.environ.get("EMSDK_ENV", ""),
        (
            os.path.join(os.environ["EMSDK"], "emsdk_env.sh")
            if os.environ.get("EMSDK")
            else ""
        ),
        os.path.join(home, "emsdk", "emsdk_env.sh"),
        os.path.join(project_dir, ".emsdk", "emsdk_env.sh"),
        # Sibling companion checkout (meshtastic-web-node / meshtasticd-wasm-node)
        # bootstraps an emsdk under ./emsdk via tools/setup-emsdk.sh.
        os.path.join(project_dir, "..", "meshtastic-web-node", "emsdk", "emsdk_env.sh"),
        os.path.join(
            project_dir, "..", "meshtasticd-wasm-node", "emsdk", "emsdk_env.sh"
        ),
    ]
    emsdk_env = next((c for c in candidates if c and os.path.isfile(c)), None)
    if not emsdk_env:
        return  # let the platform builder report the missing-SDK error

    # Source emsdk_env.sh in a subshell and copy its environment back. Using a
    # newline-delimited `env` dump is portable across GNU/BSD; the emsdk vars
    # (PATH, EMSDK, EM_CONFIG, EMSDK_NODE, ...) have no embedded newlines.
    try:
        out = subprocess.check_output(
            ["bash", "-c", "source '%s' >/dev/null 2>&1 && env" % emsdk_env],
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        print("[native-wasm] WARNING: could not source %s (%s)" % (emsdk_env, exc))
        return

    for line in out.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            os.environ[key] = value

    if shutil.which("emcc"):
        print("[native-wasm] emsdk environment imported from %s" % emsdk_env)
    else:
        print(
            "[native-wasm] WARNING: sourced %s but emcc still not on PATH" % emsdk_env
        )


_import_emsdk_environment()
