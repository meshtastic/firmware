#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
Import("env")
platform = env.PioPlatform()

if platform.name == "native":
    env.Replace(PROGNAME="meshtasticd")
else:
    from readprops import readProps
    prefsLoc = env["PROJECT_DIR"] + "/version.properties"
    verObj = readProps(prefsLoc)
    env.Replace(PROGNAME=f"firmware-{env.get('PIOENV')}-{verObj['long']}")

# Print the new program name for verification
print(f"PROGNAME: {env.get('PROGNAME')}")
