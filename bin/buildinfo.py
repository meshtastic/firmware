#!/usr/bin/env python3
import configparser
from readprops import readProps


verStr = readProps('version.properties')
print(f"{verStr}")
