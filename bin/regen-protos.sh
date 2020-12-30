#!/bin/bash

set -e

echo "This script requires https://jpa.kapsi.fi/nanopb/download/ version 0.4.4 to be located in the"
echo "meshtastic-device root directory if the following step fails, you should download the correct"
echo "prebuilt binaries for your computer into nanopb-0.4.4"

# the nanopb tool seems to require that the .options file be in the current directory!
cd proto
../nanopb-0.4.4/generator-bin/protoc --nanopb_out=-v:../src/mesh/generated -I=../proto *.proto

echo "Regenerating protobuf documentation - if you see an error message"
echo "you can ignore it unless doing a new protobuf release to github."
bin/regen-docs.sh