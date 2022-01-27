#!/usr/bin/env python3
import getopt
import sys
import os
from littlefs import LittleFS
from pathlib import Path

print( "Building LittleFS image..." )

argList = sys.argv[1:]
arxx = { argList[i]: argList[i+1] for i in range(0, len(argList)-1, 2) }

dataPath = arxx["-c"]
blockSize = int(arxx["-b"])
blockCount = int(arxx["-s"]) / blockSize

cwd = os.getcwd()

os.chdir(dataPath)

fileList = []
dirList = []

for (dirpath, dirnames, filenames) in os.walk('.'):
    for f in filenames:
        if (f[:1] != '.'):
            fileList.append( os.path.join(dirpath, f) )
    for d in dirnames:
        if (d[:1] != '.'):
            dirList.append( os.path.join(dirpath, d) )

fs = LittleFS(block_size=blockSize, block_count=blockCount) # create a 448kB partition

for curDir in dirList:
    print( "Creating dir " + curDir )
    fs.mkdir( curDir )
        
for curFile in fileList:
    print( "Adding file " + curFile )
    with open( curFile, 'rb' ) as f:
        data = f.read()

    with fs.open( curFile, 'wb') as fh:
        fh.write( data )

outName = argList[-1]

os.chdir(cwd)

with open(outName, 'wb') as fh:
    fh.write(fs.context.buffer)
