set -e 

echo uploading to usb1
pio run --upload-port /dev/ttyUSB1 -t upload

echo uploading to usb0
pio run --upload-port /dev/ttyUSB0 -t upload -t monitor
