

import subprocess
import configparser
import traceback
import sys

Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
config = configparser.RawConfigParser()
config.read(prefsLoc)
version = dict(config.items('VERSION'))

# Try to find current build SHA if if the workspace is clean.  This could fail if git is not installed
try:
    sha = subprocess.check_output(
        ['git', 'rev-parse', '--short', 'HEAD']).decode("utf-8").strip()
    isDirty = subprocess.check_output(
        ['git', 'diff', 'HEAD']).decode("utf-8").strip()
    suffix = sha
    if isDirty:
        suffix = sha + "-dirty"
    verStr = "{}.{}.{}.{}".format(
        version["major"], version["minor"], version["build"], suffix)
except:
    print("Unexpected error:", sys.exc_info()[0])
    traceback.print_exc()
    verStr = "{}.{}.{}".format(
        version["major"], version["minor"], version["build"])

print("Using meshtastic platform-custom.py, firmare version " + verStr)

# General options that are passed to the C and C++ compilers
projenv.Append(CCFLAGS=[
    "-DAPP_VERSION=" + verStr
])
