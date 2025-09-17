#!/bin/bash -eu

# Build Meshtastic and a few needed dependencies using clang++
# and the OSS-Fuzz required build flags.

env

cd "$SRC"
NPROC=$(nproc || echo 1)

LDFLAGS=-lpthread cmake -S "$SRC/yaml-cpp" -B "$WORK/yaml-cpp/$SANITIZER" \
	-DBUILD_SHARED_LIBS=OFF
cmake --build "$WORK/yaml-cpp/$SANITIZER" -j "$NPROC"
cmake --install "$WORK/yaml-cpp/$SANITIZER" --prefix /usr

cmake -S "$SRC/orcania" -B "$WORK/orcania/$SANITIZER" \
	-DBUILD_STATIC=ON
cmake --build "$WORK/orcania/$SANITIZER" -j "$NPROC"
cmake --install "$WORK/orcania/$SANITIZER" --prefix /usr

cmake -S "$SRC/yder" -B "$WORK/yder/$SANITIZER" \
	-DBUILD_STATIC=ON -DWITH_JOURNALD=OFF
cmake --build "$WORK/yder/$SANITIZER" -j "$NPROC"
cmake --install "$WORK/yder/$SANITIZER" --prefix /usr

cmake -S "$SRC/ulfius" -B "$WORK/ulfius/$SANITIZER" \
	-DBUILD_STATIC=ON -DWITH_JANSSON=OFF -DWITH_CURL=OFF -DWITH_WEBSOCKET=OFF
cmake --build "$WORK/ulfius/$SANITIZER" -j "$NPROC"
cmake --install "$WORK/ulfius/$SANITIZER" --prefix /usr

cd "$SRC/firmware"

PLATFORMIO_EXTRA_SCRIPTS=$(echo -e "pre:.clusterfuzzlite/platformio-clusterfuzzlite-pre.py\npost:.clusterfuzzlite/platformio-clusterfuzzlite-post.py")
STATIC_LIBS=$(pkg-config --libs --static libulfius openssl libgpiod yaml-cpp bluez --silence-errors)
export PLATFORMIO_EXTRA_SCRIPTS
export STATIC_LIBS
export PLATFORMIO_WORKSPACE_DIR="$WORK/pio/$SANITIZER"
export TARGET_CC=$CC
export TARGET_CXX=$CXX
export TARGET_LD=$CXX
export TARGET_AR=llvm-ar
export TARGET_AS=llvm-as
export TARGET_OBJCOPY=llvm-objcopy
export TARGET_RANLIB=llvm-ranlib

mkdir -p "$OUT/lib"

cp .clusterfuzzlite/*_fuzzer.options "$OUT/"

for f in .clusterfuzzlite/*_fuzzer.cpp; do
	fuzzer=$(basename "$f" .cpp)
	cp -f "$f" src/fuzzer.cpp
	pio run -vvv --environment "$PIO_ENV"
	program="$PLATFORMIO_WORKSPACE_DIR/build/$PIO_ENV/program"
	cp "$program" "$OUT/$fuzzer"

	# Copy shared libraries used by the fuzzer.
	read -d '' -ra shared_libs < <(ldd "$program" | sed -n 's/[^=]\+=> \([^ ]\+\).*/\1/p') || true
	cp -f "${shared_libs[@]}" "$OUT/lib/"

	# Build the initial fuzzer seed corpus.
	corpus_name="${fuzzer}_seed_corpus"
	corpus_generator="$PWD/.clusterfuzzlite/${corpus_name}.py"
	if [[ -f $corpus_generator ]]; then
		mkdir "$corpus_name"
		pushd "$corpus_name"
		python3 "$corpus_generator"
		popd
		zip -D "$OUT/${corpus_name}.zip" "$corpus_name"/*
	fi
done
