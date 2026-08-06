"""Link the esp_tinyusb managed components into a USB-Ethernet gadget build.

`custom_component_add = espressif/esp_tinyusb` makes pioarduino build
libespressif__esp_tinyusb.a and libespressif__tinyusb.a, but it never adds them
to pioarduino-build.py's LIBS, so the application links without them and fails
on tinyusb_driver_install / tinyusb_net_init.

The archives land in one of two places depending on whether the IDF libs were
rebuilt for this environment (project build dir) or reused from the shared
framework package (already on LIBPATH), so add the project location here and let
`-lespressif__*` in the env's build_flags resolve from whichever exists.

Doing this in a script rather than as `-L` build_flags is deliberate: PlatformIO
routes `-L` into LIBPATH but does not resolve a relative path from the project
root, so the entry silently misses. $BUILD_DIR is unambiguous.
"""

import os

Import("env")  # noqa: F821

build_dir = env.subst("$BUILD_DIR")  # noqa: F821
idf_dir = os.path.join(build_dir, "esp-idf")

for component in ("espressif__esp_tinyusb", "espressif__tinyusb"):
    path = os.path.join(idf_dir, component)
    env.Append(LIBPATH=[path])  # noqa: F821
