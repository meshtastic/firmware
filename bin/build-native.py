#!/usr/bin/env python3

import subprocess
import sys

from readprops import readProps


def run(*command, check=True):
    subprocess.run(command, check=check)


def bash(command: str, /, *, check=True):
    run("bash", "-c", command, check=check)


verObj = readProps("version.properties")

version = verObj["long"]
short_version = verObj["short"]

outdir = "release"

bash(f'rm -f "{outdir}/firmware*"')
bash(f'mkdir -p "{outdir}/"')
bash(f'rm -r "{outdir}"/*', check=False)

pathPrefix = ""
try:
    run("platformio", "pkg", "update", "--environment", "native")
except Exception as e:
    if sys.prefix != sys.base_prefix:
        raise e  # we are in a venv
    if not (sys.stdin and sys.stdin.isatty()):
        raise e  # no point asking the user
    a = ""
    while a.lower() not in ["y", "n", "yes", "no"]:
        a = input(
            "platformio had issues and you are not using a virtual environment\n"
            "would you like to setup virtualenv and retry with platformio from pip ? (y/n): "
        )
    if a.lower() not in ["y", "yes"]:
        raise e
    run("python3", "-m", "virtualenv", "venv")
    run("venv/bin/pip", "install", "platformio")
    pathPrefix = "venv/bin/"
    run(pathPrefix + "platformio", "pkg", "update", "--environment", "native")

run(pathPrefix + "pio", "run", "--environment", "native")
bash(f"cp .pio/build/native/program {outdir}/meshtasticd_linux_$(uname -m)")
bash(f"cp bin/device-install.* {outdir}/")
bash(f"cp bin/device-update.* {outdir}/")
