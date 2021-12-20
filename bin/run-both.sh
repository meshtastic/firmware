set -e 

TARG=tbeam

pio run -e $TARG

echo uploading to usb1
pio run --upload-port /dev/ttyUSB1 -t upload -e $TARG &

echo uploading to usb0
pio run --upload-port /dev/ttyUSB0 -t upload -e $TARG &

wait
