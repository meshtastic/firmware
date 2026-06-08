#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821)
#
# Whole-image LTO for nrf52840, EXCEPT the interrupt handlers.
#
# Every interrupt/exception handler is referenced only from the assembly vector
# table (gcc_startup_nrf52840.S), which LTO cannot see. So whole-program LTO judges
# the handlers dead, removes them, and the weak `b .` Default_Handler stubs prevail
# -> the first IRQ (FreeRTOS scheduler-start SVC, then GPIOTE/RTC/USBD/...) lands in
# an infinite loop and the chip hangs. Compiling the handler-bearing objects WITHOUT
# LTO lets ordinary linking keep the strong handlers (exactly like a non-LTO build):
#   - framework core (/FrameworkArduino/, /cores/nRF5/): every nrfx ISR + the
#     FreeRTOS SVC/PendSV port (vPortSVCHandler / xPortPendSVHandler).
#   - TinyUSB's nrf port: USBD_IRQHandler -- without it USB enumerates but the data
#     path hangs (the host's first transfer interrupt is never serviced).
# Everything else (app src + RadioLib/Crypto/nanopb/sensor libs/...) stays LTO'd.
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
