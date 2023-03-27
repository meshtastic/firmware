#!/usr/bin/env bash

set -e

echo "This script requires https://jpa.kapsi.fi/nanopb/download/ version 0.4.7 to be located in the"
echo "firmware root directory if the following step fails, you should download the correct"
echo "prebuilt binaries for your computer into nanopb-0.4.7"

# the nanopb tool seems to require that the .options file be in the current directory!
cd protobufs
../nanopb-0.4.7/generator-bin/protoc --nanopb_out=-v:../src/mesh/generated/ -I=../protobufs meshtastic/*.proto --experimental_allow_proto3_optional

# cd ../src/mesh/generated/meshtastic
# sed -i 's/#include "meshtastic/#include "./g' -- *

# sed -i 's/meshtastic_//g' -- *

#echo "Regenerating protobuf documentation - if you see an error message"
#echo "you can ignore it unless doing a new protobuf release to github."
#bin/regen-docs.sh
