#!/usr/bin/env bash

set -e 

# regen the design bins first
cd design
bin/generate-pngs.sh
cd ..

# assumes 50 wide, 28 high
convert design/logo/png/Mesh_Logo_Black_Small.png -background white -alpha Background src/graphics/img/icon.xbm

inkscape --batch-process -o images/compass.png -w 48 -h 48 images/location_searching-24px.svg
convert compass.png -background white -alpha Background src/graphics/img/compass.xbm

inkscape --batch-process -o images/face.png -w 13 -h 13 images/face-24px.svg

inkscape --batch-process -o images/pin.png -w 13 -h 13 images/room-24px.svg
convert pin.png -background white -alpha Background src/graphics/img/pin.xbm
