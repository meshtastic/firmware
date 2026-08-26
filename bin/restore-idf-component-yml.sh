#!/usr/bin/env bash
# Restore the shared Arduino framework idf_component.yml after building an env
# that uses `custom_component_add` (currently only meshnology_w12_usbnet).
#
# Why this is needed: pioarduino's component_manager.py resolves
# `custom_component_add` by rewriting
#   ~/.platformio/packages/framework-arduinoespressif32/idf_component.yml
# in place, taking a one-time pristine backup as `.orig`. Its restore machinery
# (restore_pioarduino_build_py) only covers pioarduino-build.py, NOT this file.
# Worse, `custom_component_add` is not part of the HybridCompile cache key
# (see the comment block in extra_scripts/esp32_pre.py), so the added
# dependency silently leaks into every other ESP32 env built afterwards.
#
# Run this after building the usbnet env and before building anything else.

set -euo pipefail

PKG="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32"
YML="$PKG/idf_component.yml"
ORIG="$YML.orig"

if [ ! -f "$ORIG" ]; then
	# No backup to restore from. That is fine when the manifest is pristine
	# (framework not yet built, or already restored) - but if a tinyusb
	# dependency is present, the shared framework is contaminated and we have
	# nothing to restore it from: fail loudly instead of reporting success.
	if [ -f "$YML" ] && grep -qE 'espressif/(esp_)?tinyusb' "$YML"; then
		echo "Cannot restore $YML: it contains a tinyusb dependency but the pristine backup $ORIG is missing." >&2
		echo "Reinstall the framework package (or delete it and rebuild any non-usbnet env) to regenerate a clean manifest." >&2
		exit 1
	fi
	echo "No $ORIG - nothing to restore (framework not yet built?)." >&2
	exit 0
fi

if grep -q "tinyusb" "$YML" 2>/dev/null; then
	echo "idf_component.yml contains a tinyusb dependency; restoring from .orig"
else
	echo "idf_component.yml has no tinyusb dependency; restoring anyway to normalize"
fi

cp "$ORIG" "$YML"
echo "Restored $YML from $ORIG"
