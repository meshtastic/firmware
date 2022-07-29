

import subprocess
import configparser
import traceback
import sys
from readprops import readProps

Import("env")
env.Replace( MKSPIFFSTOOL=env.get("PROJECT_DIR") + '/bin/mklittlefs.py' )
try:
    import littlefs
except ImportError:
    env.Execute("$PYTHONEXE -m pip install littlefs-python")

Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verObj = readProps(prefsLoc)
print("Using meshtastic platformio-custom.py, firmware version " + verObj['long'])
# print("path is" + ','.join(sys.path))

# General options that are passed to the C and C++ compilers
projenv.Append(CCFLAGS=[
    "-DAPP_VERSION=" + verObj['long'],
    "-DAPP_VERSION_SHORT=" + verObj['short']    
])
