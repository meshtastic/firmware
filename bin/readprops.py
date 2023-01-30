

import subprocess
import configparser
import traceback
import sys


def readProps(prefsLoc):
    """Read the version of our project as a string"""

    config = configparser.RawConfigParser()
    config.read(prefsLoc)
    version = dict(config.items('VERSION'))
    verObj = dict(short = "{}.{}.{}".format(version["major"], version["minor"], version["build"]),
        long = "unset")

    # Try to find current build SHA if if the workspace is clean.  This could fail if git is not installed
    try:
        sha = subprocess.check_output(
            ['git', 'rev-parse', '--short', 'HEAD']).decode("utf-8").strip()
        isDirty = subprocess.check_output(
            ['git', 'diff', 'HEAD']).decode("utf-8").strip()
        suffix = sha
        # if isDirty:
        #     # short for 'dirty', we want to keep our verstrings source for protobuf reasons
        #     suffix = sha + "-d"
        verObj['long'] = "{}.{}.{}.{}".format(
            version["major"], version["minor"], version["build"], suffix)
    except:
        # print("Unexpected error:", sys.exc_info()[0])
        # traceback.print_exc()
        verObj['long'] = verObj['short']

    # print("firmware version " + verStr)
    return verObj
# print("path is" + ','.join(sys.path))
