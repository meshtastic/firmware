#!/usr/bin/env python3
import sys

from readprops import readProps

verObj = readProps("version.properties")
propName = sys.argv[1]
print(f"{verObj[propName]}")
