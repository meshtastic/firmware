#!/usr/bin/env python

"""Generate the CI matrix"""

import configparser
import json
import os
import sys

rootdir = "variants/"

options = sys.argv[1:]

outlist = []

if len(options) < 1:
    print(json.dumps(outlist))
    exit()

for subdir, dirs, files in os.walk(rootdir):
    for file in files:
        if file == "platformio.ini":
            config = configparser.ConfigParser()
            config.read(subdir + "/" + file)
            for c in config.sections():
                if c.startswith("env:"):
                    section = config[c].name[4:]
                    if "extends" in config[config[c].name]:
                        if config[config[c].name]["extends"] == options[0] + "_base":
                            if "board_level" in config[config[c].name]:
                                if (
                                    config[config[c].name]["board_level"] == "extra"
                                ) & ("extra" in options):
                                    outlist.append(section)
                            else:
                                outlist.append(section)

print(json.dumps(outlist))
