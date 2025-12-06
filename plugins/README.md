# Meshtastic Plugin Authoring Guide

## Installation & Setup

> **Note**: Until the plugin system is officially accepted, you must use `pip install` followed by `mpm init` in the firmware folder to apply the plugin patches to the firmware. You'll need to do this to older versions of the firmware even if this is eventually accepted into core.

```bash
# Install MPM
pip install mesh-plugin-manager

# From the firmware folder (directory containing platformio.ini)
mpm init
```

The build system automatically uses MPM during PlatformIO builds to include all plugins and generate protobuf bindings.

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

## Automatic Protobuf Generation

MPM automatically scans for and generates protobuf files:

- **Discovery**: Recursively scans plugin directories for `.proto` files
- **Options file**: Auto-detects matching `.options` files (e.g., `mymodule.proto` → `mymodule.options`)
- **Generation**: Uses `nanopb` tooling to generate C++ files
- **Output**: Generated files are placed in the same directory as the `.proto` file
- **Timing**: Runs during PlatformIO pre-build phase (configured in `platformio.ini`)

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
- The build system handles this automatically

## Module Registration

If your plugin implements a Meshtastic module, use the `#pragma MPM_MODULE` directive in your header file:

1. Add `#pragma MPM_MODULE(ClassName)` to your module's header file (`.h`)
2. Optionally specify a variable name: `#pragma MPM_MODULE(ClassName, variableName)`
3. If you specify a variable name, declare it as `extern` in your header file
4. Your module will be automatically initialized when the firmware starts

Example (without variable):

```cpp
// MyModule.h
#pragma once
#pragma MPM_MODULE(MyModule)

class MyModule : public SinglePortModule {
    // ... module definition ...
};
```

Example (with variable - for modules that need to be referenced elsewhere):

```cpp
// MyModule.h
#pragma once
#pragma MPM_MODULE(MyModule, myModule)

#include "SinglePortModule.h"

class MyModule : public SinglePortModule {
    // ... module definition ...
};

// Declare the variable as extern so other files can reference it
extern MyModule *myModule;
```

The variable will be assigned in the generated `init_dynamic_modules()` function. If you don't need to reference your module from other files, you can omit the variable name and extern declaration.

> **Note**: Module registration is optional. Plugins that don't implement Meshtastic modules (e.g., utility libraries) don't need this.

For details on writing Meshtastic modules, see the [Module API documentation](https://meshtastic.org/docs/development/device/module-api/).

## Example Plugins

- [LoBBS](https://github.com/MeshEnvy/lobbs) - an on-firmware BBS
- [LoDB](https://github.com/MeshEnvy/lodb) - a microncontroller-friendly relational database for persisting settings, data, and more
- See https://meshforge.org for more
