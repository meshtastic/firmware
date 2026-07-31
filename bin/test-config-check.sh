#!/usr/bin/env bash
# Drive a built meshtasticd against the fixtures in test/fixtures/portduino-config
# and assert what it does with each one.
#
# Why this is a shell test and not a Unity suite: both behaviours under test are
# properties of the process, not of a function. `--check` is judged by its exit
# status and its printed report, and the "normal run rejects a bad config" path
# ends in exit(EXIT_FAILURE) inside portduinoSetup() - neither is reachable from
# a native unit test that links a single translation unit.
#
# Usage:
#   ./bin/test-config-check.sh                       # auto-detect the binary
#   ./bin/test-config-check.sh path/to/meshtasticd   # explicit binary
#   MESHTASTICD_BIN=... ./bin/test-config-check.sh
#
# Exit codes: 0 = GREEN, 1 = RED. The final line is machine-readable:
#   RESULT: GREEN 42/42 assertions passed
#   RESULT: RED 2 of 42 assertions failed

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FIXTURES="$ROOT_DIR/test/fixtures/portduino-config"

BIN="${1:-${MESHTASTICD_BIN-}}"
if [[ -z $BIN ]]; then
	for candidate in "$ROOT_DIR/.pio/build/native/meshtasticd" "$ROOT_DIR/.pio/build/coverage/meshtasticd"; do
		[[ -x $candidate ]] && BIN="$candidate" && break
	done
fi
if [[ -z $BIN || ! -x $BIN ]]; then
	echo "RESULT: RED no meshtasticd binary (build one with 'pio run -e native', or pass a path)"
	exit 1
fi
# Each case runs from a directory of its own choosing, so a relative path (CI passes
# .pio/build/coverage/meshtasticd) has to be resolved before the first cd.
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"

# Note: `pio test -e native` writes its Unity test program to this same path, so after a test
# run the auto-detected binary is that program rather than the application. That turns out to be
# harmless here -- portduino's main() parses argv and runs portduinoSetup(), which prints the
# report and exit()s before setup() (Unity's entry point) is ever reached -- so every case below
# behaves identically either way. It stops being true only for a case that lets meshtasticd run
# past portduinoSetup(), which this suite deliberately never does: a config it accepts boots a
# node and blocks.

# The coverage env links ASan/LSan. Every case here ends in exit(), so LSan would
# report the still-reachable config objects and turn a passing case into a non-zero
# exit that has nothing to do with what is being asserted.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

# Keep the virtual filesystem out of the user's home, and run from a directory with
# no config.yaml in it so the fixture we name is the only config in play.
WORKDIR="$(mktemp -d -t meshcheck.XXXXXX)"
trap 'rm -rf "$WORKDIR"' EXIT

PASS=0
FAIL=0

# assert <description> <expected-exit> <fixture> <mode> [expected-substring...]
#   mode:    "check" adds --check, "check-yaml" adds --check --output-yaml,
#            "normal" runs with neither.
#   fixture: a bare name runs from a scratch directory. A name containing a slash
#            (configd-conflict/config.yaml) runs from that fixture's own directory,
#            so a relative ConfigDirectory in it resolves the way it would in situ.
assert() {
	local desc="$1" want_rc="$2" fixture="$3" mode="$4"
	shift 4

	local cwd="$WORKDIR" config="$FIXTURES/$fixture"
	if [[ $fixture == */* ]]; then
		cwd="$FIXTURES/$(dirname "$fixture")"
		config="$(basename "$fixture")"
	fi

	local args=(--config "$config" -d "$WORKDIR/fs")
	case $mode in
	check) args+=(--check) ;;
	check-yaml) args+=(--check --output-yaml) ;;
	esac

	local out rc
	# A regression that hangs must fail the test, not stall CI. Every case here is
	# expected to exit in well under a second.
	out="$(cd "$cwd" && timeout 60 "$BIN" "${args[@]}" 2>&1)"
	rc=$?

	local problems=()
	[[ $rc -ne $want_rc ]] && problems+=("exit $rc, wanted $want_rc")
	local needle
	for needle in "$@"; do
		grep -qF -- "$needle" <<<"$out" || problems+=("missing: $needle")
	done

	if [[ ${#problems[@]} -eq 0 ]]; then
		PASS=$((PASS + 1))
		echo "  ok    $desc"
	else
		FAIL=$((FAIL + 1))
		echo "  FAIL  $desc"
		for problem in "${problems[@]}"; do
			echo "          $problem"
		done
		sed 's/^/          | /' <<<"$out"
	fi
}

# A config that meshtasticd fully accepts is asserted clean AND asserted to resolve
# to the module we asked for - otherwise a silent fallback to sim would still pass.
assert_clean_module() {
	local fixture="$1" module="$2"
	assert "$module" 0 "$fixture" check \
		"Result: 0 errors, 0 warnings" \
		"Module            : $module"
}

echo "meshtasticd config-check tests"
echo "  binary: $BIN"
echo

echo "a valid config for every radio module family is clean:"
assert_clean_module module-rf95.yaml RF95
assert_clean_module module-sx1262.yaml sx1262
assert_clean_module module-sx1268.yaml sx1268
assert_clean_module module-llcc68.yaml LLCC68
assert_clean_module module-sx1280.yaml sx1280
assert_clean_module module-lr1110.yaml lr1110
assert_clean_module module-lr1120.yaml lr1120
assert_clean_module module-lr1121.yaml lr1121
assert_clean_module module-sim.yaml sim
assert_clean_module module-auto.yaml auto

echo
echo "other configs that must not be flagged:"
assert "minimal valid config" 0 valid.yaml check \
	"Result: 0 errors, 0 warnings"
assert "empty sections are not a fault" 0 empty-sections.yaml check \
	"Result: 0 errors, 0 warnings"
assert "warnings alone do not fail the run" 0 unknown-key.yaml check \
	"unknown key 'Lora.Frequency'" \
	"Result: 0 errors, 1 warning"

echo
echo "module names are matched exactly:"
assert "unknown module names the valid set" 1 module-unknown.yaml check \
	"Lora.Module 'sx1263' is not a module meshtasticd knows" \
	"Result: 1 error, 0 warnings"
assert "wrong-case module suggests the right spelling" 1 module-wrong-case.yaml check \
	"did you mean 'LLCC68'?" \
	"Result: 1 error, 0 warnings"

echo
echo "LR11xx rfswitch table:"
assert "unrecognised switch pin" 1 rfswitch-bad-pin.yaml check \
	"'DIO9' is not a recognised pin" \
	"Result: 1 error, 0 warnings"
assert "row length must match the pin count" 1 rfswitch-row-length.yaml check \
	"MODE_STBY has 2 values but 3 pins are declared" \
	"MODE_RX has 4 values but 3 pins are declared" \
	"Result: 2 errors, 0 warnings"
# Anything not exactly "HIGH" is silently treated as LOW, so lowercase "high" is a
# switch that never closes - the failure this check exists to catch.
assert "levels must be exactly HIGH or LOW" 1 rfswitch-bad-level.yaml check \
	"'high' is not HIGH or LOW" \
	"'On' is not HIGH or LOW" \
	"Result: 2 errors, 0 warnings"
assert "table with no pins list" 1 rfswitch-no-pins.yaml check \
	"has no 'pins' list, so no switch pins are driven" \
	"Result: 1 error, 0 warnings"
assert "only the first 5 pins are read" 1 rfswitch-too-many-pins.yaml check \
	"lists 6 pins but only the first 5 are read" \
	"Result: 1 error, 0 warnings"
assert "table must be a mapping" 1 rfswitch-not-a-map.yaml check \
	"Lora.rfswitch_table must be a mapping" \
	"Result: 1 error, 0 warnings"
assert "unknown MODE_ key" 1 rfswitch-unknown-mode.yaml check \
	"unknown key 'Lora.rfswitch_table.MODE_TRANSMIT'" \
	"Result: 1 error, 0 warnings"
# One level of over-indentation strands MODE_ rows under Lora: instead of under the
# table, where they do nothing. The hint is what makes that findable.
assert "MODE_ row stranded outside the table" 0 rfswitch-stranded-modes.yaml check \
	"unknown key 'Lora.MODE_RX'" \
	"It is a valid key of Lora.rfswitch_table" \
	"Result: 0 errors, 1 warning"
# A partial table is legal, but the modes left out are driven all-LOW, which for most
# modules is the shutdown state - so the report says which ones rather than leaving it
# to be discovered on air.
assert "omitted modes are called out" 0 rfswitch-partial.yaml check \
	"omits MODE_GNSS, MODE_TX_HF, MODE_TX_HP, MODE_WIFI" \
	"default to all pins LOW" \
	"Result: 0 errors, 0 warnings"

echo
echo "radio module and switch table must agree:"
assert "LR11xx without a table cannot transmit" 0 module-mismatch-lr11xx.yaml check \
	"Module is lr1121 but no Lora.rfswitch_table is set" \
	"Result: 0 errors, 1 warning"
assert "table on a non-LR11xx radio is ignored" 0 module-mismatch-sx126x.yaml check \
	"the table is only applied to LR11xx radios" \
	"Result: 0 errors, 1 warning"

echo
echo "PA gain table (TX_GAIN_LORA):"
# Both shapes are legal and they fail differently. The scalar case is a regression
# guard: an earlier version of the checker called this working config a fatal error.
assert "a bare scalar is legal" 0 txgain-scalar.yaml check \
	"Result: 0 errors, 0 warnings"
assert "a non-numeric list entry stops meshtasticd" 1 value-type-fatal-list.yaml check \
	"TX_GAIN_LORA entry 'high' is not a whole number" \
	"refuses to start" \
	"Result: 1 error, 0 warnings"
assert "entries outside the uint16 range wrap" 1 txgain-out-of-range.yaml check \
	"entry -5 does not fit the 0-65535 range" \
	"entry 70000 does not fit the 0-65535 range" \
	"Result: 2 errors, 0 warnings"
assert "points past the 22nd are dropped" 0 txgain-too-many.yaml check \
	"lists 25 points but only the first 22 are stored" \
	"Result: 0 errors, 1 warning"

echo
echo "values of the wrong type:"
# The two settings read without a fallback: a bad value throws inside loadConfig() and
# meshtasticd will not start. Before this check, --check called these files clean.
assert "no-fallback read is fatal" 1 value-type-fatal.yaml check \
	"Logging.AsciiLogs is not a true/false value" \
	"refuses to start" \
	"Result: 1 error, 0 warnings"
assert "defaulted reads are silently ignored" 0 value-type-silent.yaml check \
	"Lora.spiSpeed is not a whole number" \
	"Lora.DIO2_AS_RF_SWITCH is not a true/false value" \
	"General.MaxNodes is not a whole number" \
	"silently replaced by the default" \
	"Result: 0 errors, 4 warnings"

echo
echo "out-of-range and unit mistakes:"
# The volts/millivolts trap: every other Meshtastic surface says millivolts, so 1800 is
# the natural thing to write and it silently asks for 1800V.
assert "TCXO voltage written in millivolts" 1 tcxo-millivolts.yaml check \
	"resolves to 1800000 mV" \
	"The value is in VOLTS" \
	"Result: 1 error, 0 warnings"
assert "ports outside their usable range" 1 port-out-of-range.yaml check \
	"General.APIPort 80 is outside 1024-65535" \
	"Webserver.Port 99999 is not a usable TCP port" \
	"Result: 1 error, 1 warning"
assert "StatusMessage longer than its buffer" 0 statusmessage-long.yaml check \
	"is truncated to 79 when it is stored" \
	"Result: 0 errors, 1 warning"
# Regression guard for a crash: this used to abort meshtasticd (and --check with it)
# via an uncaught filesystem_error from directory_iterator.
assert "unreadable ConfigDirectory is reported, not fatal" 1 configdir-missing.yaml check \
	"is not a directory that can be read" \
	"Result: 1 error, 0 warnings"

echo
echo "MAC address sources:"
assert "both MACAddress and MACAddressSource" 1 mac-conflict.yaml check \
	"General.MACAddress and General.MACAddressSource are both set" \
	"Result: 1 error, 0 warnings"
assert "MACAddress that is not 12 hex digits" 1 mac-malformed.yaml check \
	"is not 12 hex digits" \
	"Result: 1 error, 0 warnings"
assert "MACAddressSource naming no interface" 0 mac-source-missing.yaml check \
	"has no /sys/class/net/nosuchiface99/address" \
	"Result: 0 errors, 1 warning"

echo
echo "pin mappings:"
assert "unknown pin sub-key" 1 pin-unknown-subkey.yaml check \
	"unknown key 'Lora.CS.chipline'" \
	"A pin mapping accepts only pin, gpiochip and line" \
	"Result: 1 error, 0 warnings"
assert "pin value that resolves to -1" 1 pin-unreadable.yaml check \
	"Lora.CS is set, but its value could not be read as a pin number" \
	"Result: 1 error, 0 warnings"

echo
echo "CH341 USB-SPI adapters:"
# The Lora pins of a ch341 device are indexes on the adapter, driven by the usermode
# driver: portduinoSetup() skips initGPIOPin() for all of them. Reporting them as
# gpiochip lines to confirm with gpioinfo is wrong on Linux and meaningless on the
# Windows and macOS hosts where a USB adapter is the only way to attach a radio.
assert "adapter pins are not reported as GPIO lines" 0 usb-ch341.yaml check \
	"CH341 adapter pins (driven over USB, not claimed from a gpiochip)" \
	"pin indexes on the CH341 itself" \
	"Module            : sx1262" \
	"Result: 0 errors, 0 warnings"
assert "gpiochip mapping on a ch341 device is ignored" 0 ch341-gpiochip.yaml check \
	"Lora.spidev is ch341" \
	"Lora.CS.gpiochip, Lora.CS.line, Lora.gpiochip are read but never used" \
	"Result: 0 errors, 1 warning"

echo
echo "structural faults:"
assert "duplicate key" 1 duplicate-key.yaml check \
	"duplicate key 'Lora.CS'" \
	"yaml-cpp keeps the FIRST occurrence" \
	"Result: 1 error, 0 warnings"
assert "section body that is not a mapping" 1 nonmap-section.yaml check \
	"'Lora' is not a mapping" \
	"Result: 1 error, 0 warnings"
assert "unknown top-level section" 1 unknown-section.yaml check \
	"unknown top-level section 'Telemetry'" \
	"Result: 1 error, 0 warnings"
assert "key left at the top level" 1 stranded-key.yaml check \
	"'spidev' is a key of Display or Lora or Touchscreen" \
	"indent it one level" \
	"Result: 1 error, 0 warnings"
assert "top level is a list" 1 top-level-list.yaml check \
	"top level is not a mapping" \
	"Result: 1 error, 0 warnings"
assert "empty file" 0 empty-file.yaml check \
	"is empty" \
	"Result: 0 errors, 1 warning"
# Only the unknown key is asserted: the accompanying "built without HUB75 support"
# error depends on whether rgbmatrix was present at build time, so it is not a stable
# expectation across builds.
assert "unknown HUB75 option" 1 hub75-unknown-key.yaml check \
	"unknown key 'Display.HUB75.Colours'"
# The point of the case: a config that will not parse must still reach the report
# with a file and a line, rather than exiting early with just "Unable to use ...".
# The asserted line number counts the fixture's comment header - the only assertion
# here that moves if you edit those comments.
assert "unparseable config still reaches the report" 1 malformed-indent.yaml check \
	"malformed-indent.yaml" \
	"could not be parsed" \
	"error at line 7" \
	"Result: 1 error, 0 warnings"

echo
echo "across a config directory:"
# Three files each opening a 'Lora:' section. Every key not repeated in the last
# one loaded is silently reset to its default - here config.yaml's TCXO voltage.
# The warning COUNT is deliberately not asserted: the two config.d files name
# different modules, so which one wins - and therefore whether the LR11xx-without-a
# switch-table warning fires - depends on the order the filesystem returns them in,
# which is the very thing this fixture exists to demonstrate.
assert "config.d overrides are reported" 0 configd-conflict/config.yaml check \
	"config.d/lora-a.yaml" \
	"config.d/lora-b.yaml" \
	"not necessarily alphabetical" \
	"files define a 'Lora:' section" \
	"The file loaded last wins" \
	"Result: 0 errors,"
# Switch tables are the one place "last wins" is false: the loader only ever writes
# HIGH, so the effective table is the OR of every file. Proven with --output-yaml.
assert "switch tables across files do not override" 1 rfswitch-sticky/config.yaml check \
	"These do NOT override each other" \
	"a HIGH from an earlier file survives a later file that sets LOW" \
	"Enable exactly one"

echo
echo "--check takes precedence over --output-yaml:"
assert "report wins over the yaml dump" 0 valid.yaml check-yaml \
	"meshtasticd configuration check" \
	"Result: 0 errors, 0 warnings"

echo
echo "normal operation rejects a bad config:"
assert "unparseable config is refused" 1 malformed-indent.yaml normal \
	"Unable to use" \
	"as config file"
assert "non-mapping section is refused" 1 nonmap-section.yaml normal \
	"Unable to use" \
	"as config file"
assert "unknown module is refused" 1 module-unknown.yaml normal \
	"Unknown Lora.Module: sx1263"
assert "MACAddress conflict is refused" 1 mac-conflict.yaml normal \
	"Cannot set both MACAddress and MACAddressSource!"
# Only meaningful on a build without rgbmatrix. A build that supports HUB75 accepts
# the panel and goes on to start a node, which is not something to assert here.
# Captured rather than piped: this run exits non-zero by design, and pipefail would
# make the pipeline look failed however the grep went.
HUB75_PROBE="$("$BIN" --check --config "$FIXTURES/hub75-unknown-key.yaml" -d "$WORKDIR/fs" 2>&1)"
if grep -qF "built without HUB75 support" <<<"$HUB75_PROBE"; then
	assert "unsupported display panel is refused" 1 hub75-unknown-key.yaml normal \
		"this build does not support HUB75"
else
	echo "  skip  unsupported display panel is refused (this build has HUB75 support)"
fi

echo
TOTAL=$((PASS + FAIL))
if [[ $FAIL -eq 0 ]]; then
	echo "RESULT: GREEN $PASS/$TOTAL assertions passed"
	exit 0
fi
echo "RESULT: RED $FAIL of $TOTAL assertions failed"
exit 1
