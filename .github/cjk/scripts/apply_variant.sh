#!/usr/bin/env bash
# Apply the variant-specific platformio.ini changes for one target.
# Run from the root of the firmware source tree.
#
# Usage: apply_variant.sh <env_name> <overlay_dir>

set -e

ENV_NAME="${1:?env name required, e.g. seeed_wio_tracker_L1}"
OVERLAY_DIR="${2:?overlay dir required}"

# Find which variant ini declares this env.
TARGET_INI=$(grep -rl --include='*.ini' "env:${ENV_NAME}\b" variants/ | head -1)
if [ -z "$TARGET_INI" ]; then
    echo "::error::No variant ini declares env:${ENV_NAME}"
    exit 1
fi

echo "Target ini: $TARGET_INI"

# Per-target strategy:
case "$ENV_NAME" in
    seeed_wio_tracker_L1)
        # Wio L1 stock has no exclusions or CJK. Full replacement.
        cp "${OVERLAY_DIR}/seeed_wio_tracker_L1.platformio.ini" "$TARGET_INI"
        echo "Replaced $TARGET_INI with full overlay."
        ;;

    gat562_mesh_trial_tracker_zhcn|gat562_mesh_trial_tracker)
        # ssp97's gat562 already enables OLED_CJK + has the right exclusions.
        # We only need to add -DJP_IME=1 to the gat562_mesh_base block.
        if grep -q "JP_IME=1" "$TARGET_INI"; then
            echo "JP_IME already set in $TARGET_INI"
        else
            python3 - "$TARGET_INI" <<'PY'
import sys, re
path = sys.argv[1]
with open(path) as f:
    src = f.read()

# Inject  -D JP_IME=1  inside the build_flags of [env:gat562_mesh_base].
# Match the whole block and append the flag before the next section header.
def inject(match):
    block = match.group(0)
    if "JP_IME=1" in block:
        return block
    # Find the build_flags = ... block and append.
    new = re.sub(
        r"(build_flags\s*=\s*\$\{nrf52840_base\.build_flags\}.*?)(\n[a-zA-Z])",
        r"\1\n  -DJP_IME=1\2",
        block, count=1, flags=re.DOTALL,
    )
    return new

src2 = re.sub(
    r"\[env:gat562_mesh_base\][^\[]*",
    inject,
    src, count=1,
)
with open(path, "w") as f:
    f.write(src2)
print(f"Injected -DJP_IME=1 into [env:gat562_mesh_base] in {path}")
PY
        fi
        ;;

    *)
        echo "::warning::Unknown target $ENV_NAME — no overlay applied."
        ;;
esac

# Always show the resolved file so the workflow log has a record.
echo "----- $TARGET_INI -----"
cat "$TARGET_INI"
