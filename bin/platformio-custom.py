

import subprocess
import configparser
import traceback
import sys
from readprops import readProps

Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verObj = readProps(prefsLoc)
print("Using meshtastic platform-custom.py, firmare version " + verObj['long'])
# print("path is" + ','.join(sys.path))

# General options that are passed to the C and C++ compilers
projenv.Append(CCFLAGS=[
    "-DAPP_VERSION=" + verObj['long'],
    "-DAPP_VERSION_SHORT=" + verObj['short']    
])
