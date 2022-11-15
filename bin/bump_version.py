#!/usr/bin/env python
"""Bump the version number"""

lines = None

with open('version.properties', 'r', encoding='utf-8') as f:
    lines = f.readlines()

with open('version.properties', 'w', encoding='utf-8') as f:
    for line in lines:
        if line.lstrip().startswith("build = "):
            words = line.split(" = ")
            ver = f'build = {int(words[1]) + 1}'
            f.write(f'{ver}\n')
        else:
            f.write(line)
