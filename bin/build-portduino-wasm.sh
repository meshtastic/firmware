#!/usr/bin/env bash
# Build the meshtasticd portduino node to WebAssembly (ARCH_PORTDUINO_WASM):
# the real firmware setup()/loop() running in a browser tab / headless node,
# driving a LoRa radio over WebUSB through a CH341 (libpinedio_webusb.c ->
# Ch341Hal -> RadioLib). Produces build/wasm/meshnode.{mjs,wasm}.
#
# This is a standalone emcc build (PlatformIO has no emcc platform). It reuses
# the firmware's PlatformIO libdeps + framework-portduino, and the wasm-only
# glue/backend in src/platform/portduino/wasm/. Per-file CACHED objects: only
# changed sources recompile; one error pass; link when clean.
#
#   bin/build-portduino-wasm.sh           # compile (cached) + link
#   bin/build-portduino-wasm.sh clean     # wipe the object cache
#
# Prereqs:
#   - emsdk: set EMSDK_ENV=/path/to/emsdk_env.sh (or have $EMSDK / ~/emsdk).
#   - native libdeps: run `pio run -e native-macos` once to populate them.
#
# set -u, NOT -e: we want the whole error wave per pass.
set -u

# Repo root (this script lives in bin/).
FW="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HERE="$FW"
WASMDIR="$FW/src/platform/portduino/wasm"

# Locate + source the Emscripten SDK. Try, in order: $EMSDK_ENV, $EMSDK,
# ~/emsdk, an in-repo .emsdk (gitignored), then a sibling meshtastic-web-node.
for cand in "${EMSDK_ENV-}" "${EMSDK-}/emsdk_env.sh" "$HOME/emsdk/emsdk_env.sh" \
	"$FW/.emsdk/emsdk_env.sh" "$FW/../meshtastic-web-node/emsdk/emsdk_env.sh"; do
	[ -n "$cand" ] && [ -f "$cand" ] && {
		source "$cand" >/dev/null 2>&1
		break
	}
done
command -v emcc >/dev/null 2>&1 || {
	echo "emcc not found. Install emsdk (https://emscripten.org) and set EMSDK_ENV=/path/to/emsdk_env.sh"
	exit 1
}

LIBDEPS="$FW/.pio/libdeps/native-macos"
[ -d "$LIBDEPS" ] || {
	echo "Missing $LIBDEPS — populate libdeps once: pio run -e native-macos"
	exit 1
}
FWPORT="$HOME/.platformio/packages/framework-portduino"
RADIOLIB="$LIBDEPS/RadioLib/src"
# native-macos/Crypto is a stale checkout missing XEdDSA.*; heltec-v3/Crypto has it.
CRYPTO="$FW/.pio/libdeps/heltec-v3/Crypto"
[ -d "$CRYPTO" ] || CRYPTO="$LIBDEPS/Crypto"
NANOPB="$LIBDEPS/Nanopb"
CRC32="$LIBDEPS/ErriezCRC32/src"
[ -d "$CRC32" ] || CRC32="$LIBDEPS/ErriezCRC32"

OUT="$FW/build/wasm"
OBJ="$FW/build/wasm-obj"
LOG="$FW/build/wasm-errors.log"
[ "${1-}" = "clean" ] && {
	rm -rf "$OBJ"
	echo "cleaned $OBJ"
	exit 0
}
mkdir -p "$OUT" "$OBJ" "$(dirname "$LOG")"
: >"$LOG"

# Expose ONLY yaml-cpp headers to the build (PortduinoGlue.cpp includes them).
# A bare -I/opt/homebrew/include would leak ulfius.h etc. and break the build, so
# build a scratch dir with a single yaml-cpp symlink to the system headers.
YAMLCPP_INC="$(pkg-config --variable=includedir yaml-cpp 2>/dev/null || true)"
[ -z "$YAMLCPP_INC" ] && command -v brew >/dev/null 2>&1 && YAMLCPP_INC="$(brew --prefix 2>/dev/null)/include"
[ -z "$YAMLCPP_INC" ] && YAMLCPP_INC="/usr/include"
YAMLSHIM="$OBJ/_yamlshim"
mkdir -p "$YAMLSHIM"
[ -e "$YAMLSHIM/yaml-cpp" ] || ln -s "$YAMLCPP_INC/yaml-cpp" "$YAMLSHIM/yaml-cpp" 2>/dev/null || true

# Generate USERPREFS_* defines from userPrefs.jsonc — the native build does this
# in bin/platformio-custom.py; we replicate its value-typing logic here.
USERPREFS_H="$OBJ/userprefs_generated.h"
python3 - "$FW/userPrefs.jsonc" "$USERPREFS_H" <<'PY'
import json, re, sys
src = open(sys.argv[1]).read()
src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
src = re.sub(r'//[^\n]*', '', src)
src = re.sub(r',(\s*[}\]])', r'\1', src)
prefs = json.loads(src)
out = ['// auto-generated from userPrefs.jsonc — do not edit', '#pragma once']
for k, v in prefs.items():
    v = str(v)
    if v.startswith('{') or v.lstrip('-').replace('.', '').isdigit() or v in ('true', 'false') or v.startswith('meshtastic_'):
        out.append(f'#define {k} {v}')
    else:
        out.append('#define %s "%s"' % (k, v.replace('\\', '\\\\').replace('"', '\\"')))
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
print(f'[userprefs] generated {len(prefs)} defines -> {sys.argv[2]}')
PY

INCLUDES=(
	-I "$WASMDIR/stubs" # FIRST: our <argp.h> stub shadows the missing glibc one
	-I "$FW/src" -I "$FW/src/mesh" -I "$FW/src/mesh/generated" -I "$FW/src/mesh/generated/meshtastic"
	-I "$FW/src/gps" -I "$FW/src/buzz" -I "$FW/src/platform/portduino"
	-I "$FW/variants/native/portduino"
	-I "$FWPORT/cores/portduino" -I "$FWPORT/cores/portduino/include" -I "$FWPORT/cores/portduino/FS"
	-idirafter "$YAMLSHIM" # yaml-cpp headers ONLY (not all of /opt/homebrew/include, which leaks ulfius.h)
	-idirafter "$FWPORT/libraries/Wire/src" -idirafter "$FWPORT/libraries/SPI/src"
	-idirafter "$FWPORT/libraries/WiFi/src" # header-only: WiFiServerAPI.h needs <WiFi.h> (.cpp excluded)
	-I "$FWPORT/ArduinoCore-API"
	# api/ holds Arduino's String.h; on a case-insensitive FS a plain -I makes
	# <string.h> resolve to String.h (breaking <cstring>/libc). -idirafter keeps
	# it as a *fallback* so system <string.h> wins but quoted "ArduinoAPI.h" still resolves.
	-idirafter "$FWPORT/ArduinoCore-API/api"
	-I "$RADIOLIB" -I "$CRYPTO" -I "$NANOPB" -I "$CRC32"
	-I "$WASMDIR/include" -I "$WASMDIR"
)
# Add every Arduino libdep (root + src/) as a FALLBACK include (-idirafter) so
# firmware lib headers (Thread.h, CRC32.h, fsm.h, OneButton.h, TinyGPS++.h, …)
# resolve without any of them shadowing system/Arduino headers.
for d in "$LIBDEPS"/*/; do
	INCLUDES+=(-idirafter "$d")
	[ -d "${d}src" ] && INCLUDES+=(-idirafter "${d}src")
done

DEFINES=(
	-DARCH_PORTDUINO -DARCH_PORTDUINO_WASM -DARDUINO=10805
	-DAPP_VERSION=2.7.0 -DAPP_VERSION_SHORT=2.7.0 -DAPP_ENV=native_web -DAPP_REPO=meshtastic
	-DRADIOLIB_EEPROM_UNSUPPORTED -DPB_ENABLE_MALLOC=1 -DPB_VALIDATE_UTF8=1
	-DUSE_THREAD_NAMES -DTINYGPS_OPTION_NO_CUSTOM_FIELDS -DMAX_THREADS=40
	-DRADIOLIB_EXCLUDE_CC1101=1 -DRADIOLIB_EXCLUDE_NRF24=1 -DRADIOLIB_EXCLUDE_RF69=1
	-DRADIOLIB_EXCLUDE_SX1231=1 -DRADIOLIB_EXCLUDE_SX1233=1 -DRADIOLIB_EXCLUDE_SI443X=1
	-DRADIOLIB_EXCLUDE_RFM2X=1 -DRADIOLIB_EXCLUDE_AFSK=1 -DRADIOLIB_EXCLUDE_BELL=1
	-DRADIOLIB_EXCLUDE_HELLSCHREIBER=1 -DRADIOLIB_EXCLUDE_MORSE=1 -DRADIOLIB_EXCLUDE_RTTY=1
	-DRADIOLIB_EXCLUDE_SSTV=1 -DRADIOLIB_EXCLUDE_AX25=1 -DRADIOLIB_EXCLUDE_DIRECT_RECEIVE=1
	-DRADIOLIB_EXCLUDE_PAGER=1 -DRADIOLIB_EXCLUDE_FSK4=1 -DRADIOLIB_EXCLUDE_APRS=1
	-DRADIOLIB_EXCLUDE_ADSB=1 -DRADIOLIB_EXCLUDE_LORAWAN=1
	-DHAS_SCREEN=0 -DMESHTASTIC_EXCLUDE_SCREEN=1
	-DMESHTASTIC_EXCLUDE_GPS=1 -DNO_GPS=1 -DNO_EXT_GPIO=1 -DMESHTASTIC_EXCLUDE_I2C=1
	-DMESHTASTIC_EXCLUDE_ACCELEROMETER=1 -DMESHTASTIC_EXCLUDE_MAGNETOMETER=1
	-DMESHTASTIC_EXCLUDE_AUDIO=1 -DMESHTASTIC_EXCLUDE_INPUTBROKER=1
	-DMESHTASTIC_EXCLUDE_MQTT=1 -DMESHTASTIC_EXCLUDE_TZ=1
	-DMESHTASTIC_EXCLUDE_REMOTEHARDWARE=1 -DMESHTASTIC_EXCLUDE_HEALTH_TELEMETRY=1
	-DMESHTASTIC_EXCLUDE_DROPZONE=1 -DMESHTASTIC_EXCLUDE_REPLYBOT=1
	-DMESHTASTIC_EXCLUDE_POWERSTRESS=1 -DMESHTASTIC_EXCLUDE_POWERMON=1
	-DMESHTASTIC_EXCLUDE_GENERIC_THREAD_MODULE=1
	-DMESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR=1 -DMESHTASTIC_EXCLUDE_DETECTIONSENSOR=1
	-DMESHTASTIC_EXCLUDE_EXTERNALNOTIFICATION=1 -DMESHTASTIC_EXCLUDE_CANNEDMESSAGES=1
	-DMESHTASTIC_EXCLUDE_STOREFORWARD=1 -DMESHTASTIC_EXCLUDE_SERIAL=1
	-DMESHTASTIC_EXCLUDE_PAXCOUNTER=1 -DMESHTASTIC_EXCLUDE_WAYPOINT=1
)
# -include sys/time.h: firmware uses gettimeofday() but relies on the native
# toolchain pulling <sys/time.h> in transitively; emscripten doesn't, so force it.
CXX=(-std=gnu++17 -Os -fexceptions -include sys/time.h -include "$USERPREFS_H" -Wno-missing-field-initializers -Wno-format -Wno-unused)
CC=(-std=gnu17 -Os -include sys/time.h -include "$USERPREFS_H")

# ---- source set (the build_src_filter, materialized; grow as linker dictates) -
FW_SRCS=()
add() { for f in "$@"; do [ -e "$f" ] && FW_SRCS+=("$f"); done; }
add "$FW/src/main.cpp" "$FW/src/PowerFSM.cpp" "$FW/src/Power.cpp" "$FW/src/airtime.cpp" \
	"$FW/src/sleep.cpp" "$FW/src/RedirectablePrint.cpp" "$FW/src/SerialConsole.cpp" \
	"$FW/src/Observer.cpp" "$FW/src/FSCommon.cpp" "$FW/src/SafeFile.cpp" \
	"$FW/src/MessageStore.cpp" "$FW/src/meshUtils.cpp" "$FW/src/memGet.cpp" "$FW/src/GpioLogic.cpp" \
	"$FW/src/PowerMon.cpp" "$FW/src/detect/ScanI2C.cpp" "$FW/src/detect/ScanI2CTwoWire.cpp" \
	"$FW/src/detect/ScanI2CConsumer.cpp" "$FW/src/SPILock.cpp" \
	"$FW/src/power/PowerHAL.cpp" "$FW/src/xmodem.cpp" "$FW/src/DisplayFormatters.cpp" \
	"$FW/src/gps/RTC.cpp" "$FW/src/buzz/buzz.cpp" "$FW/src/buzz/BuzzerFeedbackThread.cpp"
# MeshPacketSerializer (jsoncpp) replaced by a stub — JSON output is MQTT-only (excluded)
# exclude only LR2021/LR20x0 interfaces (not referenced; their template isn't instantiated)
while IFS= read -r f; do FW_SRCS+=("$f"); done < <(find "$FW/src/mesh" -maxdepth 1 \( -name '*.cpp' -o -name '*.c' \) ! -name 'LR2*Interface.cpp')
while IFS= read -r f; do FW_SRCS+=("$f"); done < <(find "$FW/src/mesh/generated" \( -name '*.c' -o -name '*.cpp' \))
while IFS= read -r f; do FW_SRCS+=("$f"); done < <(find "$FW/src/concurrency" -name '*.cpp')
add "$FW/src/platform/portduino/PortduinoGlue.cpp" "$FW/src/platform/portduino/SimRadio.cpp"
# all modules except esp32-specific (the MESHTASTIC_EXCLUDE_* defines gate the unwanted ones)
while IFS= read -r f; do FW_SRCS+=("$f"); done < <(find "$FW/src/modules" -name '*.cpp' ! -path '*/esp32/*')

LIB_SRCS=()
while IFS= read -r f; do LIB_SRCS+=("$f"); done < <(find "$RADIOLIB" -name '*.cpp')
while IFS= read -r f; do LIB_SRCS+=("$f"); done < <(find "$CRYPTO" -name '*.cpp')
LIB_SRCS+=("$NANOPB/pb_common.c" "$NANOPB/pb_encode.c" "$NANOPB/pb_decode.c")
LIB_SRCS+=("$LIBDEPS/arduino-fsm/Fsm.cpp") # PowerFSM uses arduino-fsm (Fsm/State)
LIB_SRCS+=("$LIBDEPS/ArduinoThread/Thread.cpp" "$LIBDEPS/ArduinoThread/ThreadController.cpp")
LIB_SRCS+=("$LIBDEPS/Melopero RV3028/src/Melopero_RV3028.cpp") # RTC chip driver used by gps/RTC.cpp
while IFS= read -r f; do LIB_SRCS+=("$f"); done < <(find "$CRC32" \( -name '*.cpp' -o -name '*.c' \) 2>/dev/null)

FWP_SRCS=(
	"$FWPORT/ArduinoCore-API/api/Common.cpp" "$FWPORT/ArduinoCore-API/api/Stream.cpp"
	"$FWPORT/ArduinoCore-API/api/Print.cpp" "$FWPORT/ArduinoCore-API/api/String.cpp"
	"$FWPORT/ArduinoCore-API/api/IPAddress.cpp"
	"$FWPORT/cores/portduino/PortduinoGPIO.cpp" "$FWPORT/cores/portduino/PortduinoPrint.cpp"
	"$FWPORT/cores/portduino/logging.cpp" "$FWPORT/cores/portduino/Utility.cpp"
	"$FWPORT/cores/portduino/itoa.cpp" # itoa/ultoa (AVR-style int->string)
	"$FWPORT/cores/portduino/linux/millis.cpp"
	"$FWPORT/cores/portduino/linux/LinuxSerial.cpp"                                                           # provides arduino::Serial/SimSerial (compiles under emcc)
	"$FWPORT/cores/portduino/linux/LinuxHardwareI2C.cpp" "$FWPORT/cores/portduino/linux/LinuxHardwareSPI.cpp" # Wire/SPI globals
	# Filesystem (fs::FS/fs::File/PortduinoFS) — POSIX calls run against emscripten MEMFS/IDBFS
	"$FWPORT/cores/portduino/FS/FS.cpp" "$FWPORT/cores/portduino/FS/vfs_api.cpp"
	"$FWPORT/cores/portduino/FS/PortduinoFS.cpp"
	"$FWPORT/cores/portduino/dtostrf.c" # dtostrf (AVR-style float->string)
	# WiFi globals — AdminModule reads WiFi.RSSI()/localIP() for status reporting
	"$FWPORT/libraries/WiFi/src/WiFi.cpp" "$FWPORT/libraries/WiFi/src/WiFiClient.cpp"
	"$FWPORT/libraries/WiFi/src/WiFiServer.cpp" "$FWPORT/libraries/WiFi/src/WiFiUdp.cpp"
)
SHIMS=("$WASMDIR/portduino_main_wasm.cpp" "$WASMDIR/portduino_glue_wasm.cpp" "$WASMDIR/libpinedio_webusb.c" "$WASMDIR/stubs/serializer_stub.cpp")

ALL=("${FW_SRCS[@]}" "${LIB_SRCS[@]}" "${FWP_SRCS[@]}" "${SHIMS[@]}")

# ---- per-file cached compile ------------------------------------------------
OBJS=()
built=0
cached=0
fails=()
FAILLOG="$OBJ/_failed.txt"
: >"$FAILLOG"
for src in "${ALL[@]}"; do
	[ -f "$src" ] || {
		echo "MISSING $src" | tee -a "$LOG"
		fails+=("$src")
		continue
	}
	key=$(echo "$src" | sed 's#[/. ]#_#g')
	obj="$OBJ/$key.o"
	OBJS+=("$obj")
	if [ -f "$obj" ] && [ "$obj" -nt "$src" ]; then
		cached=$((cached + 1))
		continue
	fi
	case "$src" in *.c) flags=("${CC[@]}") ;; *) flags=("${CXX[@]}") ;; esac
	if emcc "${flags[@]}" "${INCLUDES[@]}" "${DEFINES[@]}" -c "$src" -o "$obj" 2>>"$LOG"; then
		built=$((built + 1))
	else
		fails+=("$src")
		echo "$src" >>"$FAILLOG"
	fi
done

echo "==== compile: built=$built cached=$cached failed=${#fails[@]} (of ${#ALL[@]}) ===="
if [ "${#fails[@]}" -gt 0 ]; then
	echo "--- failed files ---"
	printf '  %s\n' "${fails[@]}" | sed "s#$FW/##; s#$FWPORT/##; s#$LIBDEPS/##"
	echo "--- last compile errors ($LOG) ---"
	tail -40 "$LOG"
	exit 1
fi

echo "==== all compiled — linking ===="
emcc "${OBJS[@]}" \
	-fexceptions \
	-s ASYNCIFY=1 \
	-s 'ASYNCIFY_IMPORTS=["webusb_open","webusb_transceive","webusb_digital_write","webusb_digital_read","webusb_close"]' \
	-s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=64MB -s STACK_SIZE=5MB \
	-s MODULARIZE=1 -s EXPORT_ES6=1 -s EXPORT_NAME=createMeshNode -s INVOKE_RUN=0 \
	-lidbfs.js -lnodefs.js \
	-s EXPORTED_RUNTIME_METHODS=ccall,cwrap,callMain,FS,IDBFS,NODEFS,PATH,HEAPU8,UTF8ToString,stringToUTF8 \
	-s EXPORTED_FUNCTIONS='_main,_wasm_setup,_wasm_loop_once,_wasm_fs_sync,_wasm_set_region,_wasm_api_to_radio,_wasm_api_from_radio,_wasm_api_available,_wasm_api_is_connected,_malloc,_free' \
	-s ERROR_ON_UNDEFINED_SYMBOLS=1 \
	-o "$OUT/meshnode.mjs" 2>>"$LOG"
rc=$?
echo "==== link exit $rc ===="
[ $rc -eq 0 ] && {
	echo "Built $OUT/meshnode.mjs"
	ls -la "$OUT"/meshnode.*
} || {
	echo "--- link errors ---"
	tail -50 "$LOG"
}
exit $rc
