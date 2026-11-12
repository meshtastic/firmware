#!/usr/bin/env bash
# Runs test/ctgrind/ctgrind.cpp under valgrind memcheck with the secret inputs marked undefined.
# Prints OK or LEAK per operation; exits non-zero on a leak only with CTGRIND_STRICT=1.
set -euo pipefail
cd "$(dirname "$0")/.."

ENV=coverage-crypto
OUT=".pio/ctgrind-crypto"
mkdir -p "$OUT"
command -v valgrind >/dev/null || {
	echo "valgrind not installed" >&2
	exit 2
}

if [[ ! -f compile_commands.json ]] || ! grep -q "$ENV" compile_commands.json; then
	pio run -e "$ENV" -t compiledb >/dev/null
fi
pio run -e "$ENV" >/dev/null 2>&1 || true

FLAGS=$(
	python3 -I - "$ENV" <<'PY'
import json, shlex, sys
for e in json.load(open("compile_commands.json")):
    if e["file"].endswith("src/crypto/crypto_lib.cpp") and sys.argv[1] in e.get("command", ""):
        a = shlex.split(e["command"])
        keep = [x for x in a if x.startswith(("-I", "-D", "-std=gnu++20"))] + ["-isystem/usr/include/bsd"]
        print(" ".join(shlex.quote(x) for x in keep))
        break
PY
)

# No sanitizers: valgrind cannot run under ASan.
# -O0: at -O1 clang turns the constant-time masks in Curve25519 into branches that memcheck flags.
CXXFLAGS="${CTGRIND_OPT:--O0} -g -w"
LIBSRC=$(ls ".pio/libdeps/$ENV/Crypto"/*.cpp | grep -v 'AESEsp32\|P521\|NoiseSource')
OBJS=()
i=0
for src in src/crypto/crypto_api.cpp src/crypto/crypto_lib.cpp src/mesh/HardwareRNG.cpp test/test_crypto_vectors/minimal_stubs.cpp test/ctgrind/ctgrind.cpp test/ctgrind/clock_stubs.cpp $LIBSRC; do
	o="$OUT/obj$((i++)).o"
	eval clang++ $CXXFLAGS $FLAGS -c "$src" -o "$o"
	OBJS+=("$o")
done
clang++ "${OBJS[@]}" -lpthread -o "$OUT/ctgrind"

rc=0
for op in aes128_ctr aes256_ctr aes256_ccm_seal hmac_sha256 x25519 x25519_base ed25519_basemul xeddsa_sign; do
	if valgrind -q --error-exitcode=99 --track-origins=yes "$OUT/ctgrind" "$op" >"$OUT/$op.log" 2>&1; then
		echo "OK    $op"
	else
		echo "LEAK  $op ($(grep -c 'depends on uninitialised\|Use of uninitialised' "$OUT/$op.log") reports, see $OUT/$op.log)"
		rc=1
	fi
done
[[ ${CTGRIND_STRICT:-0} == 1 ]] && exit $rc
exit 0
