#!/bin/bash
# adapted from the script linked in this very helpful article: https://enzolombardi.net/low-power-bluetooth-advertising-with-xiao-ble-and-platformio-e8e7d0da80d2

# source: https://gist.githubusercontent.com/turing-complete-labs/b3105ee653782183c54b4fdbe18f411f/raw/d86779ba7702775d3b79781da63d85442acd9de6/xiao_ble.sh
# download the core for arduino from seeedstudio. Softdevice 7.3.0, linker and variants folder are what we need
curl https://files.seeedstudio.com/arduino/core/nRF52840/Arduino_core_nRF52840.tar.bz2 -o arduino.core.1.0.0.tar.bz2
tar -xjf arduino.core.1.0.0.tar.bz2
rm arduino.core.1.0.0.tar.bz2

# Determine the target path based on the operating system
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  PLATFORMIO_PATH="$HOME/.platformio/packages/framework-arduinoadafruitnrf52"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
  # Convert Windows-style path to Unix-style path for Git Bash
  WIN_HOME_PATH=$(echo "$USERPROFILE" | sed 's/\\/\//g')
  PLATFORMIO_PATH="/$WIN_HOME_PATH/.platformio/packages/framework-arduinoadafruitnrf52"
  PLATFORMIO_PATH=$(echo "$PLATFORMIO_PATH" | sed 's/://')
else
  echo "Unsupported OS type: $OSTYPE"
  exit 1
fi

# Print the target path for debugging
echo "Copying files to: $PLATFORMIO_PATH"

# Create necessary directories if they don't exist
mkdir -p "$PLATFORMIO_PATH/cores/nRF5/linker"
mkdir -p "$PLATFORMIO_PATH/cores/nRF5/nordic/softdevice"

# Copy the needed files
cp 1.0.0/cores/nRF5/linker/nrf52840_s140_v7.ld "$PLATFORMIO_PATH/cores/nRF5/linker"
cp -r 1.0.0/cores/nRF5/nordic/softdevice/s140_nrf52_7.3.0_API "$PLATFORMIO_PATH/cores/nRF5/nordic/softdevice"

# Wait for a moment before cleanup to ensure files are not in use
sleep 5

# Forcefully clean up
rm -rf 1.0.0
if [ $? -ne 0 ]; then
  echo "Failed to remove directory, trying again..."
  sleep 2
  rm -rf 1.0.0
fi

echo done!
