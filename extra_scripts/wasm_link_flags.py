#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
#
# Firmware-specific Emscripten *link* settings for [env:native-wasm].
#
# PlatformIO routes `build_flags` to the compile step, not the link step, so the
# WASM node's link-time emscripten settings have to be appended to LINKFLAGS
# here. The generic, app-agnostic flags (Asyncify, MODULARIZE, ALLOW_MEMORY_
# GROWTH, the ES6 module shape) live in the platform-wasm builder; what belongs
# to *this firmware* is:
#
#   * EXPORT_NAME           - the ES-module factory name consumers import.
#   * EXPORTED_RUNTIME_METHODS - ccall/cwrap/callMain + FS/IDBFS/NODEFS/PATH and
#                             the string helpers the JS host + bridge drive.
#   * EXPORTED_FUNCTIONS    - the C entry points (the wasm_* API is also kept via
#                             EMSCRIPTEN_KEEPALIVE in source; _malloc/_free are
#                             needed so the host can marshal protobuf buffers).
#   * ASYNCIFY_IMPORTS      - the WebUSB seam: these imported C functions suspend
#                             the stack (Asyncify) while a WebUSB transfer awaits.
#
# Only attached to the wasm env (see extra_scripts in [env:native-wasm]); a guard keeps
# it inert if it is ever pulled into another env.
#
Import("env")

if env["PIOENV"] == "native-wasm":
    env.Append(
        LINKFLAGS=[
            "-sEXPORT_NAME=createMeshNode",
            "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,callMain,FS,IDBFS,NODEFS,PATH,HEAPU8,UTF8ToString,stringToUTF8",
            "-sEXPORTED_FUNCTIONS=_main,_wasm_setup,_wasm_loop_once,_wasm_fs_sync,"
            "_wasm_set_region,_wasm_api_to_radio,_wasm_api_from_radio,"
            "_wasm_api_available,_wasm_api_is_connected,_wasm_set_lora_module,"
            "_wasm_set_lora_usb_ids,_wasm_set_lora_usb_serial,"
            "_wasm_set_lora_dio_config,_wasm_set_lora_spi_speed,_wasm_set_lora_pin,"
            "_malloc,_free",
            "-sASYNCIFY_IMPORTS=webusb_open,webusb_transceive,webusb_digital_write,"
            "webusb_digital_read,webusb_close",
        ]
    )
