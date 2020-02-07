# using height of 50 to have 14 pixels beneath icon for text
inkscape -z -e icon.png -w 50 -h 50 icon-24px.svg
convert icon.png -background white -alpha Background ../src/icon.xbm
