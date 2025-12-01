# Plugin Development Guide

This directory houses plugins that extend the Meshtastic firmware. Plugins are automatically discovered and integrated into the build system.

## Plugin Structure

The only requirement for a plugin is that it must have a `./src` directory:

```
plugins/
└── myplugin/
    └── src/
        ├── MyModule.h
        ├── MyModule.cpp
        └── mymodule.proto
```

- Plugin directory name can be anything
- All source files must be placed in `./src`
- Only files in `./src` are compiled (the root plugin directory and all other subdirectories are excluded from the build)

## Python Dependencies

The Mesh Plugin Manager (MPM) is installed via pip. To use MPM commands:

```bash
# Install MPM
pip install mesh-plugin-manager

# From the firmware repo root (directory containing platformio.ini)
mpm <command>
```

The build system automatically uses MPM during PlatformIO builds.

## Automatic Protobuf Generation

For convenience, the Meshtastic Plugin Manager (MPM) automatically scans for and generates protobuf files:

- **Discovery**: MPM recursively scans plugin directories for `.proto` files
- **Options file**: Auto-detects matching `.options` files (e.g., `mymodule.proto` → `mymodule.options`)
- **Generation**: Uses `nanopb` tooling to generate C++ files
- **Output**: Generated files are placed in the same directory as the `.proto` file
- **Timing**: Runs during PlatformIO pre-build phase (configured in `platformio.ini`)

**Note**: You can use the Mesh Plugin Manager CLI via `mpm` to inspect or manage plugins.

Example protobuf structure:

```
src/plugins/myplugin/src/
├── mymodule.proto      # Protobuf definition
├── mymodule.options    # Nanopb options (optional)
├── mymodule.pb.h       # Generated header
└── mymodule.pb.c       # Generated implementation
```

## Include Path Setup

The plugin's `src/` directory is automatically added to the compiler's include path (`CPPPATH`) during build:

- Headers in `src/` can be included directly: `#include "MyModule.h"`
- No need to specify relative paths from other plugin files
- The build system handles this automatically via `bin/mpm.py`

## Module Registration

If your plugin implements a Meshtastic module, you can use the automatic registration system:

1. Include `ModuleRegistry.h` in your module `.cpp` file
2. Place `MESHTASTIC_REGISTER_MODULE(ModuleClassName)` at the end of your implementation file
3. Your module will be automatically initialized when the firmware starts

Example:

```cpp
#include "MyModule.h"
#include "ModuleRegistry.h"

// ... module implementation ...

MESHTASTIC_REGISTER_MODULE(MyModule);
```

**Note**: Module registration is optional. Plugins that don't implement Meshtastic modules (e.g., utility libraries) don't need this.

For details on writing Meshtastic modules, see the [Module API documentation](https://meshtastic.org/docs/development/device/module-api/).

## Example Plugin

See the `lobbs` plugin for a complete example that demonstrates:

- Protobuf definitions with options file
- Module implementation with automatic registration
- Proper source file organization
