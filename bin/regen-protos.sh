#!/usr/bin/env bash

set -e

echo "Regenerating protobuf artifacts via protobufs/scripts/build.sh."
echo "Ensure required commands are available: node, npx, git."
echo "Artifacts will be produced under protobufs/build."
echo

# the nanopb tool seems to require that the .options file be in the current directory!
cd protobufs
./scripts/build.sh
cp -a build/c/. ../src/mesh/generated/