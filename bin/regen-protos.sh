#!/usr/bin/env bash

set -e

echo "This script requires https://jpa.kapsi.fi/nanopb/download/ version 0.4.9 to be located in the"
echo "firmware root directory if the following step fails, you should download the correct"
echo "prebuilt binaries for your computer into nanopb-0.4.9"

# protoc-gen-fieldmeta (needs Go) emits the field_metadata registry; firmware keeps only `sensitive`.
FIELDMETA_DIR="$(mktemp -d)"
trap 'rm -rf "$FIELDMETA_DIR"' EXIT
go -C protobufs/tools/protoc-gen-fieldmeta build -o "$FIELDMETA_DIR/protoc-gen-fieldmeta" .

# the nanopb tool seems to require that the .options file be in the current directory!
cd protobufs
../nanopb-0.4.9/generator-bin/protoc --experimental_allow_proto3_optional \
	"--plugin=protoc-gen-fieldmeta=$FIELDMETA_DIR/protoc-gen-fieldmeta" \
	"--nanopb_out=-S.cpp -v:../src/mesh/generated/" \
	"--fieldmeta_out=target=c,attributes=sensitive:../src/mesh/generated/" \
	-I=../protobufs meshtastic/*.proto
