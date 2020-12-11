
Import("projenv")

import configparser
prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
config = configparser.RawConfigParser()
config.read(prefsLoc)
version = dict(config.items('VERSION'))
verStr = "{}.{}.{}".format(version["major"], version["minor"], version["build"])

print(f"Using meshtastic platform-custom.py, firmare version {verStr}")

# General options that are passed to the C and C++ compilers
projenv.Append(CCFLAGS=[
    f"-DAPP_VERSION={verStr}"
    ])
