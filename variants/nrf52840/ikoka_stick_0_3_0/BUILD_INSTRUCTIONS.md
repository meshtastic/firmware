# Building Firmware for IKOKA STICK 0.3.0

This guide will help you build the firmware and generate a UF2 file for flashing to your IKOKA STICK 0.3.0 board.

## Prerequisites

1. **PlatformIO** - You can install it in one of these ways:
   - **Option A (Recommended)**: Install PlatformIO IDE extension in VS Code
     - Install [VS Code](https://code.visualstudio.com/)
     - Install the "PlatformIO IDE" extension from the VS Code marketplace
   - **Option B**: Install PlatformIO Core (CLI) standalone
     - Follow instructions at: https://platformio.org/install/cli

2. **Python 3** - Required for PlatformIO (usually comes with PlatformIO installation)

3. **Git** - To clone/update the repository (if you haven't already)

## Building the Firmware

### Method 1: Using VS Code with PlatformIO Extension (Easiest)

1. **Open the firmware directory in VS Code**
   ```bash
   cd firmware
   code .
   ```

2. **Select the build environment**
   - Click on the PlatformIO icon in the left sidebar
   - In the "PROJECT TASKS" section, expand `nrf52840` → `ikoka_stick_0_3_0`
   - Click on "Build" (or use the hammer icon in the bottom toolbar)

3. **Wait for the build to complete**
   - The build process will take several minutes
   - You'll see progress in the terminal at the bottom

4. **Find the UF2 file**
   - The UF2 file will be located at:
     ```
     .pio/build/ikoka_stick_0_3_0/firmware-ikoka_stick_0_3_0-<version>.uf2
     ```
   - You can also find it by clicking "PROJECT TASKS" → `ikoka_stick_0_3_0` → "General" → "Build Files"

### Method 2: Using PlatformIO CLI (Command Line)

1. **Open a terminal/command prompt**
   - Navigate to the firmware directory:
     ```bash
     cd firmware
     ```

2. **Build the firmware**
   ```bash
   pio run -e ikoka_stick_0_3_0
   ```
   
   Or if using PlatformIO Core directly:
   ```bash
   platformio run -e ikoka_stick_0_3_0
   ```

3. **Wait for the build to complete**
   - This will take several minutes
   - You'll see compilation progress

4. **Find the UF2 file**
   - The UF2 file will be in:
     ```
     .pio/build/ikoka_stick_0_3_0/firmware-ikoka_stick_0_3_0-<version>.uf2
     ```

## Locating the UF2 File

After a successful build, the UF2 file will be located at:

**Windows:**
```
firmware\.pio\build\ikoka_stick_0_3_0\firmware-ikoka_stick_0_3_0-<version>.uf2
```

**Linux/Mac:**
```
firmware/.pio/build/ikoka_stick_0_3_0/firmware-ikoka_stick_0_3_0-<version>.uf2
```

The `<version>` will be something like `2.7.17.xxxxx` based on the firmware version.

## Flashing the UF2 File to Your Board

### Method 1: Drag and Drop (Easiest for nRF52840)

1. **Put your IKOKA STICK 0.3.0 into bootloader mode**
   - Connect the board via USB
   - Double-press the RESET button quickly (or follow your board's specific bootloader entry method)
   - The board should appear as a USB drive named "XIAO-SENSE" or similar

2. **Copy the UF2 file**
   - Simply drag and drop the `.uf2` file onto the USB drive
   - The board will automatically flash and reboot

3. **Wait for completion**
   - The USB drive will disappear when flashing is complete
   - The board will reboot with the new firmware

### Method 2: Using PlatformIO Upload

1. **Connect your board via USB**

2. **Upload using PlatformIO**
   - In VS Code: Click "Upload" in the PlatformIO tasks
   - Or via CLI:
     ```bash
     pio run -e ikoka_stick_0_3_0 -t upload
     ```

## Troubleshooting

### Build Errors

- **"Environment not found"**: Make sure you're using the correct environment name: `ikoka_stick_0_3_0`
- **Missing dependencies**: Run `pio pkg install` to install required packages
- **Python errors**: Ensure Python 3 is installed and accessible

### Upload Errors

- **Board not detected**: 
  - Check USB connection
  - Install USB drivers if needed (nRF52840 uses standard USB CDC)
  - Try a different USB cable/port

- **Bootloader not entering**:
  - Try double-pressing RESET button quickly
  - Some boards require holding BOOT button while pressing RESET
  - Check your board's specific bootloader entry method

### UF2 File Not Generated

- Check the build output for errors
- Ensure the build completed successfully (look for "SUCCESS" message)
- Check that you're building for the correct environment: `ikoka_stick_0_3_0`

## Quick Build Command Reference

```bash
# Build firmware
pio run -e ikoka_stick_0_3_0

# Build and upload
pio run -e ikoka_stick_0_3_0 -t upload

# Clean build (removes old build files)
pio run -e ikoka_stick_0_3_0 -t clean

# Build and show verbose output
pio run -e ikoka_stick_0_3_0 -v
```

## Next Steps

After successfully flashing:

1. **Connect via serial monitor** (115200 baud) to see boot messages
2. **Check for "SX1268 init success"** message
3. **Verify radio initialization** completed without errors
4. **Test communication** with another Meshtastic node

## Additional Resources

- [Meshtastic Build Documentation](https://meshtastic.org/docs/development/firmware/build)
- [Meshtastic Flashing Guide](https://meshtastic.org/docs/getting-started/flashing-firmware/)
- [IKOKA STICK Documentation](https://ndoo.sg/projects:amateur_radio:meshtastic:diy_devices:ikoka_stick)
