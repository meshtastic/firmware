# using height of 50 to have 14 pixels beneath icon for text
inkscape -z -e icon.png -w 50 -h 50 icon-24px.svg
convert icon.png -background white -alpha Background ../src/icon.xbm

inkscape -z -e compass.png -w 48 -h 48 location_searching-24px.svg
convert compass.png -background white -alpha Background ../src/compass.xbm

inkscape -z -e face.png -w 13 -h 13 face-24px.svg

inkscape -z -e pin.png -w 13 -h 13 room-24px.svg
convert pin.png -background white -alpha Background ../src/pin.xbm
