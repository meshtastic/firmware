import configparser
import subprocess
import os
run_number = os.getenv('GITHUB_RUN_NUMBER', '0')
build_location = os.getenv('BUILD_LOCATION', 'local')

def readProps(prefsLoc):
    """Read the version of our project as a string"""

    config = configparser.RawConfigParser()
    config.read(prefsLoc)
    version = dict(config.items("VERSION"))
    verObj = dict(
        short="{}.{}.{}-alphasigma".format(version["major"], version["minor"], version["build"]),
        long="unset",
        deb="unset",
    )

    # Try to find current build SHA if if the workspace is clean.  This could fail if git is not installed
    try:
        # Pin abbreviation length to keep local builds and CI matching (avoid auto-shortening)
        sha = (
            subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"])
            .decode("utf-8")
            .strip()
        )
        isDirty = (
            subprocess.check_output(["git", "diff", "HEAD"]).decode("utf-8").strip()
        )
        suffix = sha
        # long version uses the base numeric version (no custom suffix) so it fits
        # in the 18-byte firmware_version protobuf field: "2.7.23.0cab43f" = 14 chars ✓
        base_short = "{}.{}.{}".format(version["major"], version["minor"], version["build"])
        verObj["long"] = "{}.{}".format(base_short, suffix)
        verObj["deb"] = "{}.{}~{}{}".format(verObj["short"], run_number, build_location, sha)
    except:
        verObj["long"] = verObj["short"]
        verObj["deb"] = "{}.{}~{}".format(verObj["short"], run_number, build_location)

    # print("firmware version " + verStr)
    return verObj


# print("path is" + ','.join(sys.path))