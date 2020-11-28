#!/bin/bash

echo "This script requires https://jpa.kapsi.fi/nanopb/download/ version 0.4.1"
# the nanopb tool seems to require that the .options file be in the current directory!
cd proto
../../nanopb-0.4.1-linux-x86/generator-bin/protoc --nanopb_out=-v:../src/mesh -I=../proto portnums.proto mesh.proto
