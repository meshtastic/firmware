# ARCH_PORTDUINO_WASM — meshtasticd in WebAssembly (LoRa over WebUSB)

Builds the full portduino firmware (`setup()`/`loop()`) to WebAssembly with
Emscripten, so a real Meshtastic node runs in a browser tab (or headless Node)
and drives a LoRa radio over **WebUSB** through a CH341 USB-to-SPI bridge — the
same `Ch341Hal` path the desktop `meshtasticd` uses, with the libusb backend
swapped for a WebUSB one. The desktop/native portduino build is untouched.

## Layout (this dir is excluded from the native PlatformIO `build_src_filter`)

| file                       | role                                                                                                                        |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `portduino_glue_wasm.cpp`  | hardcoded LoRa config (no YAML), MEMFS/IDBFS mount, region/MAC helpers, and the `wasm_api_*` PhoneAPI bridge                |
| `portduino_main_wasm.cpp`  | `wasm_setup()` / `wasm_loop_once()` — JS drives the cooperative loop                                                        |
| `libpinedio_webusb.c`      | WebUSB libpinedio backend (sync C ↔ async WebUSB via Asyncify `EM_ASYNC_JS`)                                                |
| `include/libpinedio-usb.h` | the 12-fn libpinedio API the backend implements                                                                             |
| `stubs/`                   | `argp.h` shim + jsoncpp serializer stub (MQTT-only, excluded)                                                               |
| `js/`                      | the WebUSB runtime: `bridge.js` (implements the C backend's imports), `ch341.js` (CH341 transport), `protocol.js` (framing) |

In-tree, six firmware sources carry small `#ifdef ARCH_PORTDUINO_WASM` guards
(single-threaded cooperative sleep, continuous RX, region default, RNG, etc.):
`src/main.cpp`, `src/mesh/{NodeDB,SX126xInterface,InterfacesTemplates,LR11x0Interface,HardwareRNG}.cpp`,
`src/platform/portduino/PortduinoGlue.cpp`. None affect non-wasm builds.

## Build

Prereqs:

- **Emscripten SDK** — set `EMSDK_ENV=/path/to/emsdk_env.sh` (or have `$EMSDK` / `~/emsdk`).
- **Native libdeps** — run `pio run -e native-macos` once so RadioLib/Crypto/Nanopb/etc. are present.

```sh
bin/build-portduino-wasm.sh          # cached per-file emcc compile + link
bin/build-portduino-wasm.sh clean    # wipe the object cache
```

Output: `build/wasm/meshnode.mjs` + `meshnode.wasm` (ES module, Asyncify, exports
`_wasm_setup`, `_wasm_loop_once`, `_wasm_fs_sync`, `_wasm_set_region`,
`_wasm_api_to_radio`, `_wasm_api_from_radio`, `_wasm_api_available`,
`_wasm_api_is_connected`).

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
drain `FromRadio` with `wasm_api_from_radio(out,max)` — the firmware's own
`PhoneAPI`, unframed. The official `@meshtastic/core` SDK drives it through a
~40-line in-process transport (see the `meshtastic-web-node` repo, which hosts
the dev server, the SDK-UI page, the headless node-usb runner, and the TCP :4403
bridge for the Python CLI). WebUSB is Chromium-only.
