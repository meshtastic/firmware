# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports

Import("env")
# Custom HEX from ELF
env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(
        " ".join(
            [
                "$OBJCOPY",
                "-O",
                "ihex",
                "-R",
                ".eeprom",
                "$BUILD_DIR/${PROGNAME}.elf",
                "$BUILD_DIR/${PROGNAME}.hex",
            ]
        ),
        "Building $BUILD_DIR/${PROGNAME}.hex",
    ),
)
