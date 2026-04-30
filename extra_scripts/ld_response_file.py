#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports

# force linker response file instead of command line arguments

Import("env")


def wrap_with_tempfile(command_key):
    command = env.get(command_key)
    if not command or not isinstance(command, str):
        return
    if "TEMPFILE(" in command:
        return
    env.Replace(**{command_key: "${TEMPFILE('%s')}" % command})


# Force SCons to spill long commands into response files on this target.
env.Replace(MAXLINELENGTH=8192)

for key in ("LINKCOM", "CXXLINKCOM", "SHLINKCOM", "SHCXXLINKCOM"):
    wrap_with_tempfile(key)
