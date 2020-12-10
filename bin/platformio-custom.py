
Import("projenv")

import configparser
config = configparser.RawConfigParser()
config.read(projenv["PROJECT_DIR"] + "/version.properties")
version = dict(config.items('VERSION'))
verStr = "{}.{}.{}".format(version["major"], version["minor"], version["build"])

print(f"Using meshtastic platform-custom.py, firmare version {verStr}")

# General options that are passed to the C and C++ compilers
projenv.Append(CCFLAGS=[
    f"-DAPP_VERSION={verStr}"
    ])