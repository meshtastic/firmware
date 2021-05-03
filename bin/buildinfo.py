#!/usr/bin/env python3
import configparser
from readprops import readProps


verObj = readProps('version.properties')
print(f"{verObj['long']}")
