#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821)
#
# Whole-image LTO for nrf52840 (~-60KB vs no-LTO; ~-23KB beyond src-only LTO),
# EXCEPT the objects that own interrupt/exception handlers.
#
# Every ISR is referenced only from the assembly vector table (gcc_startup_nrf52840.S),
# which LTO cannot see -> whole-program LTO judges the handlers dead, removes them, and
# the weak `b .` Default_Handler stubs prevail -> the first IRQ (FreeRTOS scheduler-start
# SVC, then GPIOTE/RTC/USBD/...) lands in an infinite loop and the chip hangs. Compiling
# the handler-bearing objects WITHOUT LTO lets ordinary linking keep the strong handlers
# (exactly like a non-LTO build); everything else (app src + RadioLib/Crypto/nanopb/...)
# stays LTO'd:
#   - framework core (/FrameworkArduino/, /cores/nRF5/): every nrfx ISR + the FreeRTOS
#     SVC/PendSV port (vPortSVCHandler / xPortPendSVHandler).
#   - TinyUSB's nrf port (Adafruit_TinyUSB_nrf.cpp): USBD_IRQHandler -- without it USB
#     enumerates but the data path hangs.
#
# HW-validated: RAK4631 (SX1262) and muzi-base (LR1121) both boot and init their radios.
import os

Import("env")

env.Append(LINKFLAGS=["-flto", "-flto-partition=1to1"])

# Private include dir for the TinyUSB nrf-port re-compile below (tusb_option.h etc.).
_fw = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52") or ""
_tinyusb_src = os.path.join(_fw, "libraries", "Adafruit_TinyUSB_Arduino", "src")

FRAMEWORK = ("/FrameworkArduino/", "/cores/nRF5/")
USB_ISR = "Adafruit_TinyUSB_nrf"  # defines USBD_IRQHandler


def _no_lto(node):
    try:
        path = node.get_abspath()
    except Exception:
        path = str(node)
    path = path.replace(
        "\\", "/"
    )  # normalize Windows backslashes so the substring matches below work cross-platform
    if USB_ISR in path:
        return env.Object(
            node,
            CCFLAGS=env["CCFLAGS"] + ["-fno-lto"],
            CPPPATH=env["CPPPATH"] + [_tinyusb_src],
        )
    if any(s in path for s in FRAMEWORK):
        return env.Object(node, CCFLAGS=env["CCFLAGS"] + ["-fno-lto"])
    return node


env.AddBuildMiddleware(_no_lto)
