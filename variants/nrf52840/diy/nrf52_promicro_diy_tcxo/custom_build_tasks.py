# Simplifies DIY InkHUD builds, with presets for several common E-Ink displays
# - build using custom task in Platformio's "Project Tasks" panel
# - build with `pio run -e <variant> -t build_weact_154` (or similar)

# Silence trunk's objections to the import statements
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821)

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")
Import("projenv")

# Custom targets
# These wrappers just run the normal build task under a different target name
# We intercept the build later on, based on the target name
env.AddTarget(
    name="build_weact_154",
    dependencies=["buildprog"],
    actions=None,
    title='Build (WeAct 1.54")',
)
env.AddTarget(
    name="build_weact_213",
    dependencies=["buildprog"],
    actions=None,
    title='Build (WeAct 2.13")',
)
env.AddTarget(
    name="build_weact_290",
    dependencies=["buildprog"],
    actions=None,
    title='Build (WeAct 2.9")',
)
env.AddTarget(
    name="build_weact_420",
    dependencies=["buildprog"],
    actions=None,
    title='Build (WeAct 4.2")',
)

# Check whether a build was started via one of our custom targets above

if "build_weact_154" in COMMAND_LINE_TARGETS:
    print('Building for WeAct 1.54" Display')
    projenv["CPPDEFINES"].append(("INKHUD_BUILDCONF_DRIVER", "ZJY200200_0154DAAMFGN"))
    projenv["CPPDEFINES"].append(("INKHUD_BUILDCONF_DISPLAYRESILIENCE", "15"))

elif "build_weact_213" in COMMAND_LINE_TARGETS:
    print('Building for WeAct 2.13" Display')
    projenv["CPPDEFINES"].append(("INKHUD_BUILDCONF_DRIVER", "HINK_E0213A289"))
    projenv["CPPDEFINES"].append(("INKHUD_BUILDCONF_DISPLAYRESILIENCE", "10"))

elif "build_weact_290" in COMMAND_LINE_TARGETS:
    print('Building for WeAct 2.9" Display')
    projenv["CPPDEFINES"].append(("INKHUD_BUILDCONF_DRIVER", "ZJY128296_029EAAMFGN"))
    projenv["CPPDEFINES"].append(("INKHUD_BUILDCONF_DISPLAYRESILIENCE", "15"))

elif "build_weact_420" in COMMAND_LINE_TARGETS:
    print('Building for WeAct 4.2" Display')
    projenv["CPPDEFINES"].append(("INKHUD_BUILDCONF_DRIVER", "HINK_E042A87"))
    projenv["CPPDEFINES"].append(("INKHUD_BUILDCONF_DISPLAYRESILIENCE", "15"))
