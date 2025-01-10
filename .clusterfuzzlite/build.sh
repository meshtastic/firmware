#!/bin/bash -eu

# Build Meshtastic and a few needed dependencies using clang++
# and the OSS-Fuzz required build flags.

env

cd "$SRC"
NPROC=$(nproc || echo 1)

LDFLAGS=-lpthread cmake -S "$SRC/yaml-cpp" -B "$SRC/yaml-cpp/build" \
	-DBUILD_SHARED_LIBS=OFF
cmake --build "$SRC/yaml-cpp/build" -j "$NPROC"
cmake --install "$SRC/yaml-cpp/build" --prefix /usr

cmake -S "$SRC/orcania" -B "$SRC/orcania/build" \
	-DBUILD_STATIC=ON
cmake --build "$SRC/orcania/build" -j "$NPROC"
cmake --install "$SRC/orcania/build" --prefix /usr

cmake -S "$SRC/yder" -B "$SRC/yder/build" \
	-DBUILD_STATIC=ON -DWITH_JOURNALD=OFF
cmake --build "$SRC/yder/build" -j "$NPROC"
cmake --install "$SRC/yder/build" --prefix /usr

cmake -S "$SRC/ulfius" -B "$SRC/ulfius/build" \
	-DBUILD_STATIC=ON -DWITH_JANSSON=OFF -DWITH_CURL=OFF -DWITH_WEBSOCKET=OFF
cmake --build "$SRC/ulfius/build" -j "$NPROC"
cmake --install "$SRC/ulfius/build" --prefix /usr

cd "$SRC/firmware"

PLATFORMIO_EXTRA_SCRIPTS=$(echo -e "pre:.clusterfuzzlite/platformio-clusterfuzzlite-pre.py\npost:.clusterfuzzlite/platformio-clusterfuzzlite-post.py")
STATIC_LIBS=$(pkg-config --libs --static libulfius openssl libgpiod yaml-cpp bluez --silence-errors)
export PLATFORMIO_EXTRA_SCRIPTS
export STATIC_LIBS
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
	cp ".pio/build/$PIO_ENV/program" "$OUT/$fuzzer"

	# Copy shared libraries used by the fuzzer.
	read -ra shared_libs < <(ldd ".pio/build/$PIO_ENV/program" | sed -n 's/[^=]\+=> \([^ ]\+\).*/\1/p')
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
