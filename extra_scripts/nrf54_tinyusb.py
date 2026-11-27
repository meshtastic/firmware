#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports

from os.path import isdir, join

Import("env")

# With USE_TINYUSB the core's Arduino.h includes the TinyUSB Arduino headers (Serial is a CDC port).
# The nRF52 platform's builder adds their directory itself, the nRF54 one does not (yet). Added from
# here rather than as -I in build_flags: ${platformio.packages_dir} expands with backslashes on
# Windows, which the build flag parser takes as escapes. A pre: script, so libraries inherit it.
framework_dir = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf54")
tinyusb_arduino = join(framework_dir or "", "libraries", "Adafruit_TinyUSB_Arduino", "src", "arduino")
if framework_dir and isdir(tinyusb_arduino):
    env.Append(CPPPATH=[tinyusb_arduino])
