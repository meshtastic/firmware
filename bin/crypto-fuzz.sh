#!/usr/bin/env bash
# Builds the libFuzzer targets in test/fuzz_crypto with clang and runs each for a short time.
# Usage: bin/crypto-fuzz.sh [seconds-per-target]   (default 15, capped at 60)
# Reuses the include paths and defines pio computes for [env:coverage-crypto].
set -euo pipefail
cd "$(dirname "$0")/.."

SECS="${1:-15}"
((SECS > 60)) && SECS=60
ENV=coverage-crypto
OUT=".pio/fuzz-crypto"
mkdir -p "$OUT/corpus"

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

CXXFLAGS="-O1 -g -w -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize=vptr -fno-sanitize-recover=undefined"
LIBDIR=".pio/libdeps/$ENV/Crypto"
LIBSRC=$(ls "$LIBDIR"/*.cpp | grep -v 'AESEsp32\|P521\|NoiseSource')
OBJS=()
i=0
for src in src/crypto/crypto_api.cpp src/crypto/crypto_lib.cpp src/mesh/HardwareRNG.cpp test/test_crypto_vectors/minimal_stubs.cpp $LIBSRC; do
	o="$OUT/obj$((i++)).o"
	eval clang++ $CXXFLAGS $FLAGS -c "$src" -o "$o"
	OBJS+=("$o")
done

FW=$(find ".pio/build/$ENV" -maxdepth 1 -name 'lib*.a')
for target in ccm_open x25519 ed25519_verify; do
	eval clang++ $CXXFLAGS $FLAGS -c "test/fuzz_crypto/$target.cpp" -o "$OUT/$target.o"
	clang++ -fsanitize=fuzzer,address,undefined "$OUT/$target.o" "${OBJS[@]}" -Wl,--start-group $FW -Wl,--end-group -lgcov -lpthread -o "$OUT/$target"
	mkdir -p "$OUT/corpus/$target"
	"$OUT/$target" -max_total_time="$SECS" -max_len=2048 -print_final_stats=1 "$OUT/corpus/$target" 2>&1 | tail -4
done
