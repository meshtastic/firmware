# trunk-ignore-all(flake8/F821)
# trunk-ignore-all(ruff/F821)

Import("env")

# NOTE: This is not currently used, but can serve as an example on how to write extra_scripts

# print("Current CLI targets", COMMAND_LINE_TARGETS)
# print("Current Build targets", BUILD_TARGETS)
# print("CPP defs", env.get("CPPDEFINES"))
# print(env.Dump())

# Adafruit.py in the platformio build tree is a bit naive and always enables their USB stack for building.  We don't want this.
# So come in after that python script has run and disable it.  This hack avoids us having to fork that big project and send in a PR
# which might not be accepted. -@geeksville

lib_builders = env.get("__PIO_LIB_BUILDERS", None)
if lib_builders is not None:
    print("Disabling Adafruit USB stack")
    for k in lib_builders:
        if k.name == "Adafruit TinyUSB Library":
            libenv = k.env
            # print(f"{k.name }: { libenv.Dump() } ")
            # libenv["CPPDEFINES"].remove("USBCON")
            libenv["CPPDEFINES"].remove("USE_TINYUSB")

# Custom actions when building program/firmware
# env.AddPreAction("buildprog", callback...)
