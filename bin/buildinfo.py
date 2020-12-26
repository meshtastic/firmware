#!/usr/bin/env python3
import configparser

config = configparser.RawConfigParser()
config.read('version.properties')

version = dict(config.items('VERSION'))

verStr = "{}.{}.{}".format(version["major"], version["minor"], version["build"])

print(f"{verStr}")
