#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821)
#
# Whole-image LTO for nrf52840 (~-60KB; ~-23KB beyond src-only LTO), EXCEPT the objects
# that own interrupt/exception handlers.
#
# Every ISR is referenced only from the assembly vector table (gcc_startup_nrf52840.S),
# which LTO cannot see -> whole-program LTO judges the handlers dead, removes them, and
# the weak `b .` Default_Handler stubs prevail -> the IRQ lands in an infinite loop and the
# chip hangs (or the peripheral silently stalls). Compiling the handler-bearing objects
# WITHOUT LTO lets ordinary linking keep the strong handlers; everything else stays LTO'd:
#   - framework core (/FrameworkArduino/, /cores/nRF5/): every nrfx ISR + the FreeRTOS
#     SVC/PendSV port.
#   - TinyUSB nrf port (Adafruit_TinyUSB_nrf.cpp): USBD_IRQHandler (USB data path).
#   - library .cpp files that own a vector ISR (would otherwise be silently dropped):
#       bluefruit.cpp     -> SD_EVT/SWI2_EGU2  (SoftDevice BLE-event delivery -- advertising
#                            hangs without it)
#       Wire_nRF52.cpp    -> SPIM0/TWIM0 + SPIM1/TWIM1 (interrupt-driven I2C/SPI)
#       PDM.cpp           -> PDM_IRQHandler   (PDM microphone)
#       RotaryEncoder.cpp -> QDEC_IRQHandler  (hardware quadrature/rotary encoder)
#
# HW-validated: RAK4631 (SX1262) + muzi-base (LR1121). To find dropped ISRs after a deps
# bump: nm the .elf for `_IRQHandler$` symbols marked `W` and grep the libs for who defines
# them.
import glob
import os

Import("env")

env.Append(LINKFLAGS=["-flto", "-flto-partition=1to1"])

# The -fno-lto re-compiles below run with the global env, which lacks the framework's
# bundled-library include dirs -- and those libs cross-include each other (Wire pulls in
# Adafruit_TinyUSB.h, which pulls in SPI.h, ...). Add every bundled-lib dir (+ its src/) so
# the re-compiles resolve without chasing headers one at a time.
_fw = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52") or ""
_extra_inc = []
for _d in sorted(glob.glob(os.path.join(_fw, "libraries", "*"))):
    if os.path.isdir(_d):
        _extra_inc.append(_d)
        if os.path.isdir(os.path.join(_d, "src")):
            _extra_inc.append(os.path.join(_d, "src"))

FRAMEWORK = ("/FrameworkArduino/", "/cores/nRF5/")
USB_ISR = "Adafruit_TinyUSB_nrf"  # USBD_IRQHandler
# Library .cpp files that define vector-table ISRs (the rest of their lib stays LTO'd):
LIB_ISR = ("/bluefruit.cpp", "/Wire_nRF52.cpp", "/PDM.cpp", "/RotaryEncoder.cpp")


def _no_lto(node):
    try:
        path = node.get_abspath()
    except Exception:
        path = str(node)
    path = path.replace(
        "\\", "/"
    )  # normalize Windows backslashes so the matches below work cross-platform
    if (
        USB_ISR in path
        or any(s in path for s in FRAMEWORK)
        or any(s in path for s in LIB_ISR)
    ):
        return env.Object(
            node,
            CCFLAGS=env["CCFLAGS"] + ["-fno-lto"],
            CPPPATH=env["CPPPATH"] + _extra_inc,
        )
    return node


env.AddBuildMiddleware(_no_lto)
