## Setup env
python3 -m venv venv     
source venv/bin/activate   

## Test on device

### Station G2
1. Enable Serial Debug:
# Edit variants/esp32s3/station-g2/platformio.ini line 22:
# Change: -DARDUINO_USB_MODE=0  
# To:     -DARDUINO_USB_MODE=1
2. Rebuild and Flash:
pio run -e station-g2 -t upload
3. Connect Serial:
pio device monitor --baud 115200
  

### Heltec v3

pio run -e heltec-v3 -t upload

pio device monitor --baud 115200

## If protobufs need updating 
pip install protobuf grpcio-tools

cd protobufs
protoc --experimental_allow_proto3_optional --plugin=protoc-gen-nanopb=../.pio/libdeps/station-g2/Nanopb/generator/protoc-gen-nanopb --nanopb_out="-S.cpp
  -v:../src/mesh/generated/" -I=../protobufs meshtastic/module_config.proto