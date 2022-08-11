#!/usr/bin/env python
"""Bump the version number"""

version_filename = "setup.py"

lines = None

with open(version_filename, 'r', encoding='utf-8') as f:
    lines = f.readlines()

with open(version_filename, 'w', encoding='utf-8') as f:
    for line in lines:
        if line.lstrip().startswith("version="):
            # get rid of quotes around the version
            line = line.replace('"', '')
            # get rid of trailing comma
            line = line.replace(",", "")
            # split on '='
            words = line.split("=")
            # split the version into parts (by period)
            v = words[1].split(".")
            ver = f'{v[0]}.{v[1]}.{int(v[2]) + 1}'
            f.write(f'    version="{ver}",\n')
        else:
            f.write(line)
