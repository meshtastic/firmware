
Import("projenv")

import configparser
prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
print(f"Preferences in {prefsLoc}")
try:
	config = configparser.RawConfigParser()
	config.read(prefsLoc)
	version = dict(config.items('VERSION'))
	verStr = "{}.{}.{}".format(version["major"], version["minor"], version["build"])
except:
	print("Can't read preferences, using 0.0.0")
	verStr = "0.0.0"

print(f"Using meshtastic platform-custom.py, firmare version {verStr}")

# General options that are passed to the C and C++ compilers
projenv.Append(CCFLAGS=[
    f"-DAPP_VERSION={verStr}"
    ])
