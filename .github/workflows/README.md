# Meshtastic Firmware Build Workflows

This directory contains the GitHub Actions workflows for building Meshtastic firmware for all supported platforms.

## Workflow Structure

- `firmware_build.yml` - Main workflow orchestrator
- `build_esp32.yml` - ESP32 platform builds
- `build_nrf52.yml` - Nordic NRF52 platform builds
- `build_rpi2040.yml` - Raspberry Pi RP2040 builds
- `build_stm32.yml` - STM32 platform builds

## Features

- **Parallel Building**: Matrix strategy for efficient builds across platforms
- **Caching**: Optimized caching of PlatformIO dependencies
- **Quality Checks**: Firmware size and memory analysis
- **Artifact Management**: Organized by architecture with debug symbols
- **PR Integration**: Automated feedback and artifact links on PRs

## Triggers

The workflows are triggered by:
- Push to `master`, `develop`, and `event/*` branches
- Pull requests targeting these branches
- Manual workflow dispatch

## Artifacts

Each build produces the following artifacts:
- Firmware binaries (`.bin`, `.hex`, `.uf2`)
- Debug symbols (`.elf`)
- Memory analysis reports (for STM32)
- LittleFS images (where applicable)
- Installation scripts

## Usage

### Manual Trigger

1. Go to Actions → Firmware Build
2. Click "Run workflow"
3. Select branch and options
4. Click "Run workflow"

### Automated Builds

- PRs automatically trigger builds
- Successful builds post download links in PR comments
- Release builds create GitHub releases with all artifacts

### Local Development

For local development, use the same build commands as the workflows:
```bash
# Install dependencies
pip install platformio

# Build for specific board
platformio run -e <board_name>
```

## Release Process

When triggered via workflow_dispatch on master:
1. Builds all architectures
2. Creates GitHub release
3. Uploads artifacts
4. Updates meshtastic.github.io