# ARCH_PORTDUINO_WASM - meshtasticd in WebAssembly (LoRa over WebUSB)

Builds the full portduino firmware (`setup()`/`loop()`) to WebAssembly with
Emscripten, so a real Meshtastic node runs in a browser tab (or headless Node)
and drives a LoRa radio over **WebUSB** through a CH341 USB-to-SPI bridge - the
same `Ch341Hal` path the desktop `meshtasticd` uses, with the libusb backend
swapped for a WebUSB one. The desktop/native portduino build is untouched.

## Layout (this dir is excluded from the native PlatformIO `build_src_filter`)

| file                       | role                                                                                                                                     |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `portduino_glue_wasm.cpp`  | LoRa config (MeshToad default + `wasm_set_lora_*` setters, no YAML), VFS mount, region/MAC helpers, and the `wasm_api_*` PhoneAPI bridge |
| `portduino_main_wasm.cpp`  | `wasm_setup()` / `wasm_loop_once()` - JS drives the cooperative loop                                                                     |
| `libpinedio_webusb.c`      | WebUSB libpinedio backend (sync C ↔ async WebUSB via Asyncify `EM_ASYNC_JS`)                                                             |
| `include/libpinedio-usb.h` | the 12-fn libpinedio API the backend implements                                                                                          |
| `stubs/`                   | `argp.h` shim + jsoncpp serializer stub (MQTT-only, excluded)                                                                            |
| `js/`                      | the WebUSB runtime: `bridge.js` (implements the C backend's imports), `ch341.js` (CH341 transport), `protocol.js` (framing)              |

In-tree, six firmware sources carry small `#ifdef ARCH_PORTDUINO_WASM` guards
(single-threaded cooperative sleep, continuous RX, region default, RNG, etc.):
`src/main.cpp`, `src/mesh/{NodeDB,SX126xInterface,InterfacesTemplates,LR11x0Interface,HardwareRNG}.cpp`,
`src/platform/portduino/PortduinoGlue.cpp`. None affect non-wasm builds.

## Build

This is a normal PlatformIO env (`[env:native-wasm]`) built with the
[meshtastic/platform-wasm](https://github.com/meshtastic/platform-wasm) platform
(emcc/em++), exactly like any other board target.

Prereq: an **Emscripten SDK** on `PATH` - `source <emsdk>/emsdk_env.sh` (or
`export EMSDK=<path>`) so the platform builder can locate `emcc`.

```sh
pio run -e native-wasm            # emcc compile + Asyncify link
pio run -e native-wasm -t clean   # wipe the build dir
```

Output: `.pio/build/native-wasm/meshnode.mjs` + `meshnode.wasm` (ES module, Asyncify,
factory `createMeshNode`, exports `_wasm_setup`, `_wasm_loop_once`,
`_wasm_fs_sync`, `_wasm_set_region`, `_wasm_api_to_radio`, `_wasm_api_from_radio`,
`_wasm_api_available`, `_wasm_api_is_connected`, the `_wasm_set_lora_*` setters).

## Run

The C backend imports `webusb_*` functions; `js/bridge.js` implements them on top
of `js/ch341.js`. Minimal host flow:

```js
import createMeshNode from "./meshnode.mjs";
import { CH341 } from "./js/ch341.js";
import { createCH341Bridge } from "./js/bridge.js";

const dev = (await CH341.request()).device; // WebUSB device picker (Chromium)
const Module = await createMeshNode({ noInitialRun: true });
Module.ch341 = createCH341Bridge(Module, dev); // wire WebUSB before boot
await Module.ccall("wasm_setup", null, [], [], { async: true });
const pump = async () => {
  await Module.ccall("wasm_loop_once", "number", [], [], { async: true });
  setTimeout(pump, 5);
};
pump();
```

**API control:** feed a `ToRadio` protobuf with `wasm_api_to_radio(ptr,len)` and
drain `FromRadio` with `wasm_api_from_radio(out,max)` - the firmware's own
`PhoneAPI`, unframed. The official `@meshtastic/core` SDK drives it through a
~40-line in-process transport (see the `meshtasticd-wasm-node` repo, which hosts
the dev server, the SDK-UI page, the headless node-usb runner, and the TCP :4403
bridge for the Python CLI). WebUSB is Chromium-only.

**Reboot:** the firmware can't restart itself in wasm, so a reboot (admin/phone
command, factory reset, or the 60 s stuck-TX watchdog) hands off to the host. In
a browser it calls `location.reload()` - NodeDB state survives via IDBFS, so the
node comes back with the same identity. Headless, provide a `Module.onReboot`
callback to handle it (re-instantiate the module, `process.exit()` for a
supervisor to restart, etc.); without one it just logs and keeps running.

```js
const Module = await createMeshNode({ noInitialRun: true });
Module.onReboot = () => process.exit(0); // optional; headless restart policy
```
