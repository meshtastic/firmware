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
# A post-link guard (bottom of this file) fails the build if a critical handler was dropped
# anyway -- so a future deps bump or a new ISR-owning library becomes a red build, not a field
# hang. To hunt a dropped ISR by hand: nm the .elf for `_IRQHandler$` symbols marked `W`, then
# grep the libs/framework for who defines them.
#
# HW-validated: RAK4631 (SX1262) + muzi-base (LR1121).
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
    )  # normalize Windows backslashes so matches work cross-platform
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


# --- post-link guard: catch a dropped ISR handler at build time (CI footgun protection) ----
# After every link, fail the build if one of these critical vector-table handlers resolved to
# the weak `b .` Default_Handler stub -- i.e. LTO (or a deps bump, or a new ISR-owning library
# that nobody added to LIB_ISR) silently dropped it. A dropped handler hangs the chip the
# instant that IRQ fires; this turns a field hang into a red build. CI builds every nrf52840
# target, so this runs on every PR automatically. If a board deliberately stops using one of
# these, edit the tuples on purpose.
_REQUIRED_STRONG = (
    "SWI2_EGU2_IRQHandler",  # SoftDevice BLE event (SD_EVT) -- advertising & connections
    "GPIOTE_IRQHandler",  # GPIO interrupts: radio DIO + buttons
    "RTC1_IRQHandler",  # FreeRTOS scheduler tick
)
# Owned by the TinyUSB stack, so only required when the board builds with USB at all.
# Boards without native USB wiring (e.g. wio-sdk-wm1110's CH340 UART) strip TinyUSB via
# disable_adafruit_usb.py / unflagging USE_TINYUSB, leaving these legitimately weak.
_REQUIRED_STRONG_USB = (
    "USBD_IRQHandler",  # USB CDC (serial console + 1200bps DFU trigger)
    "POWER_CLOCK_IRQHandler",  # USB power events (VBUS detect/ready) via TinyUSB hal
)

_tc = env.PioPlatform().get_package_dir("toolchain-gccarmnoneeabi") or ""
_NM = os.path.join(_tc, "bin", "arm-none-eabi-nm")
if not os.path.isfile(_NM):
    _NM = "arm-none-eabi-nm"  # fall back to PATH


def _assert_isr_handlers_survived(source, target, env):
    import subprocess
    import sys

    try:
        # Resolve the ELF at build time; target[0] is the buildprog alias, not the file.
        elf = env.subst("$BUILD_DIR/${PROGNAME}.elf")
        out = subprocess.check_output([_NM, elf], universal_newlines=True)
    except Exception as exc:  # tooling hiccup: warn loudly, don't wedge the build
        print("nrf52_lto: WARNING - ISR-handler guard skipped (nm failed: %s)" % exc)
        return
    # nm line: "<addr> <type> <symbol>". type 'T'/'t' = strong (good); 'W'/'w' = weak stub.
    kind = {}
    for line in out.split("\n"):
        f = line.split()
        if len(f) >= 3 and f[-1].endswith("_IRQHandler"):
            kind[f[-1]] = f[-2]
    required = list(_REQUIRED_STRONG)
    defines = [
        str(d[0] if isinstance(d, tuple) else d) for d in env.get("CPPDEFINES", [])
    ]
    if "USE_TINYUSB" in defines:
        required += _REQUIRED_STRONG_USB
    dropped = [h for h in required if kind.get(h, "W").upper() != "T"]
    if dropped:
        sys.stderr.write(
            "\n*** nrf52 LTO guard: interrupt handler(s) DROPPED: %s ***\n"
            "Each resolved to the weak Default_Handler stub, so the chip hangs when that IRQ\n"
            "fires. Compile the .cpp that defines the handler with -fno-lto by adding it to\n"
            "LIB_ISR in extra_scripts/nrf52_lto.py. Find the owner of FOO_IRQHandler with:\n"
            "  grep -rl FOO_IRQHandler <framework-arduinoadafruitnrf52>/{libraries,cores}\n\n"
            % ", ".join(dropped)
        )
        from SCons.Script import Exit

        Exit(1)  # canonical SCons build-abort -> red build
    print(
        "nrf52_lto: ISR-handler guard OK -- %d critical handlers strong" % len(required)
    )


# Attach to the phony "buildprog" alias, NOT the .elf file node: SCons can skip a post-action
# on a file target during an incremental relink (observed), but the buildprog alias runs every
# build -- so the guard fires on local incremental rebuilds and clean CI builds alike.
env.AddPostAction("buildprog", _assert_isr_handlers_survived)
