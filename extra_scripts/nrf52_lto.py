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

# The ISR -fno-lto re-compiles below run with the global env, which lacks the framework's
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


# The active board variant file owns strong overrides of the core's weak initVariant()/
# earlyInitVariant()/lateInitVariant() stubs (cores/nRF5/main.cpp). Same failure mode as the ISRs
# above: whole-image LTO mis-resolves the call (made from the -fno-lto framework core) to the empty
# WEAK stub, so the board's early hardware setup -- e.g. driving PIN_3V3_EN to power the LoRa/sensor
# rail -- silently never runs and the radio probe finds no chip. Compiling the variant without LTO
# lets ordinary linking pick the strong override. HW-proven on nrf52_promicro_diy_tcxo (boot-trace
# 2026-06-30). NOTE: only the real board variant (built from /variants/...) -- NOT the guarded
# src/platform/extra_variants/*/variant.cpp no-op stubs, which don't tolerate the -fno-lto recompile.
#
# Anchor to THIS repo's variants/ tree: a board variant lives at
# <PROJECT_DIR>/variants/<arch>/<board>/variant.cpp. Anchoring deliberately excludes
#   - PlatformIO's framework-owned .../framework-arduinoadafruitnrf52/variants/*/variant.cpp
#   - the src/platform/extra_variants/*/variant.cpp stubs (they live under src/, not variants/)
# both of which also end in "/variant.cpp" but must NOT be recompiled here.
#
# CAUTION: middleware nodes come from the SCons variant-dir mirror, so node.get_abspath() is
# $BUILD_DIR/variants/.../variant.cpp -- NOT the repo path. Matching the anchor against the
# mirrored path never hits, which silently re-enables LTO on the variant (the post-link guard
# below turns that into a red build). srcnode() undoes the mirror and yields the real source
# path -- the same trick piobuild.py itself uses for middleware pattern matching.
_PROJECT_VARIANTS = (
    env.subst("$PROJECT_DIR").replace("\\", "/").rstrip("/") + "/variants/"
)


def _is_board_variant(node, path):
    try:
        src = node.srcnode().get_abspath().replace("\\", "/")
    except Exception:
        src = path
    return (
        src.endswith("/variant.cpp")
        and src.startswith(_PROJECT_VARIANTS)
        and "extra_variants" not in src
    )


# projenv is the construction env PlatformIO uses to compile project sources (src/ + the board
# variant). Unlike the bare framework env captured above, it carries the -DAPP_VERSION... flags
# and the generated-protobuf include path (pb.h) that bin/platformio-custom.py appends *after*
# this pre-script's eval. It isn't exported yet at eval time, but it is by the time the build
# middleware fires -- so fetch it lazily from SCons' export registry, falling back to env.
_projenv_cache = []


def _get_projenv():
    if not _projenv_cache:
        try:
            from SCons.Script.SConscript import global_exports

            _projenv_cache.append(global_exports.get("projenv", env))
        except Exception:
            _projenv_cache.append(env)
    return _projenv_cache[0]


def _variant_ccflags(target, source, env, for_signature):
    # Deferred CCFLAGS for the board variant recompile. SCons calls this while substituting the
    # compile command (build phase), so projenv's CCFLAGS now include the -DAPP_VERSION... flags
    # that bin/platformio-custom.py appends as a POST extra_script. -fno-lto goes last to override
    # the inherited -flto and keep the variant's strong initVariant() out of whole-image LTO.
    return list(_get_projenv()["CCFLAGS"]) + ["-fno-lto"]


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
    if _is_board_variant(node, path):
        # The board variant is a project source, not a framework object: it can #include
        # configuration.h/sleep.h, which need the -DAPP_VERSION... define and the generated-
        # protobuf include path (pb.h). Recompile it with projenv, which carries both.
        #
        # TIMING: those -DAPP_VERSION... flags are appended to projenv by bin/platformio-custom.py,
        # which is an unprefixed extra_script -> PlatformIO runs it as a POST script, i.e. AFTER
        # $BUILD_SCRIPT, where this build middleware already fired. So projenv["CCFLAGS"] read HERE
        # is a pre-append snapshot with no -DAPP_VERSION -> the recompile dies with
        # "APP_VERSION must be set". Defer the read to a callable construction variable: SCons
        # invokes it during command substitution (after every SConscript, post-scripts included),
        # by which point projenv carries the version flags. -fno-lto is appended last so it wins.
        build_env = _get_projenv()
        return build_env.Object(
            node,
            CCFLAGS=_variant_ccflags,
            CPPPATH=build_env["CPPPATH"] + _extra_inc,
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


# --- post-link guard #2: the board variant's strong overrides must reach the ELF -----------
# The active variant.cpp overrides the core's weak initVariant() (cores/nRF5/main.cpp). If the
# -fno-lto middleware above stops matching it -- e.g. a path-shape change; the middleware sees
# $BUILD_DIR-mirrored nodes, and anchoring against the repo path already broke the match once
# (2026-07) -- LTO resolves the core's call to the empty weak stub, the board's early hardware
# setup silently vanishes (promicro DIY: PIN_3V3_EN rail never driven -> SX1262 CHIP_NOT_FOUND),
# and the build stays green. Turn that into a red build:
#   1. the linked variant.cpp.o must not be an LTO object (proves the -fno-lto recompile fired);
#   2. any override the object defines strong must resolve strong in the ELF.
_VARIANT_OVERRIDES = (
    "_Z11initVariantv",
)  # extend if the core grows more weak variant hooks


def _assert_variant_survived(source, target, env):
    import subprocess
    import sys

    objs = glob.glob(
        os.path.join(env.subst("$BUILD_DIR"), "variants", "**", "variant.cpp.o"),
        recursive=True,
    )
    if not objs:
        return  # env links no in-repo board variant
    problems = []
    elf_kind = {}
    obj_kind = {}
    try:
        for obj in objs:
            # An LTO object carries .gnu.lto_* sections; their names live in the shstrtab,
            # so a byte scan is a sufficient (and toolchain-free) detector.
            with open(obj, "rb") as f:
                if b".gnu.lto" in f.read():
                    problems.append(
                        "%s was compiled WITH LTO -- the -fno-lto middleware did not match it"
                        % obj
                    )

        def _kinds(nm_out):
            kinds = {}
            for line in nm_out.split("\n"):
                f = line.split()
                if len(f) >= 3:
                    kinds[f[-1]] = f[-2]
            return kinds

        elf = env.subst("$BUILD_DIR/${PROGNAME}.elf")
        elf_kind = _kinds(subprocess.check_output([_NM, elf], universal_newlines=True))
        for obj in objs:
            obj_kind.update(
                _kinds(subprocess.check_output([_NM, obj], universal_newlines=True))
            )
    except Exception as exc:  # tooling hiccup (e.g. an nm that can't read slim LTO
        # objects): warn and skip the symbol comparison, but never let it mask a
        # violation the .gnu.lto byte-scan above already recorded in `problems`.
        print("nrf52_lto: WARNING - variant guard incomplete (%s)" % exc)
        if not problems:
            return
    for sym in _VARIANT_OVERRIDES:
        if (
            obj_kind.get(sym, "").upper() == "T"
            and elf_kind.get(sym, "W").upper() != "T"
        ):
            problems.append(
                "%s is strong in variant.cpp.o but weak/absent in the ELF "
                "(LTO resolved the core's call to the empty weak stub)" % sym
            )
    if problems:
        sys.stderr.write(
            "\n*** nrf52 LTO guard: board variant DROPPED from the image ***\n%s\n"
            "The variant's early hardware setup (initVariant) will never run on this board.\n"
            "Check _is_board_variant() in extra_scripts/nrf52_lto.py -- middleware nodes are\n"
            "$BUILD_DIR-mirrored; match srcnode() paths, not node.get_abspath().\n\n"
            % "\n".join("  - " + p for p in problems)
        )
        from SCons.Script import Exit

        Exit(1)
    print("nrf52_lto: variant guard OK -- board variant kept out of LTO")


# Attach to the phony "buildprog" alias, NOT the .elf file node: SCons can skip a post-action
# on a file target during an incremental relink (observed), but the buildprog alias runs every
# build -- so the guard fires on local incremental rebuilds and clean CI builds alike.
env.AddPostAction("buildprog", _assert_isr_handlers_survived)
env.AddPostAction("buildprog", _assert_variant_survived)
