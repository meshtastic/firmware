#!/bin/bash
source ~/.bashrc

# Meshtastic Nightly Build Script.
#  McHamster (jm@casler.org)
#
# This is the script that is used for the nightly build server.
#
# It's probably not useful for most people, but you may want to run your own
#   nightly builds.
#
# The last line of ~/.bashrc contains an inclusion of platformio in the path.
#   Without this, the build script won't run from the crontab:
#
#    export PATH="$HOME/.platformio/penv/bin:$PATH" 
#
# The crontab contains:
#  0 2 * * * cd ~/meshtastic/github/meshtastic && source "~/.bashrc"; ./build-nightly.sh > ~/cronout.txt 2> ~/cronout.txt

cd Meshtastic-device

git pull

bin/build-all.sh

date_stamp=$(date +'%Y-%m-%d')

cd ..

# TODO: Archive the same binaries used by the build-all script.
#zip -r meshtastic_device_nightly_${date_stamp} Meshtastic-device/release/latest/bins
cp Meshtastic-device/release/archive/`ls -t ./Meshtastic-device/release/archive/| head -1` meshtastic_device_nightly_${date_stamp}.zip

# Copy the file to the webserver
scp meshtastic_device_nightly_${date_stamp}.zip jm@10.11.12.20:/volume1/web/meshtastic/nightly_builds/

# Delete the local copy
rm meshtastic_device_nightly_${date_stamp}.zip
