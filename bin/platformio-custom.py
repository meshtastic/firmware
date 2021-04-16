

import subprocess
import configparser
import traceback
import sys
from readprops import readProps

Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verStr = readProps(prefsLoc)
print("Using meshtastic platform-custom.py, firmare version " + verStr)
# print("path is" + ','.join(sys.path))

# General options that are passed to the C and C++ compilers
projenv.Append(CCFLAGS=[
    "-DAPP_VERSION=" + verStr
])
