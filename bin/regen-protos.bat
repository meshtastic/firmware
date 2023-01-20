cd protobufs && ..\nanopb-0.4.7\generator-bin\protoc.exe --nanopb_out=-v:..\src\mesh\generated -I=..\protobufs ..\protobufs\meshtastic\*.proto

cd ../src/mesh/generated/meshtastic
sed -i 's/#include "meshtastic/#include "./g' *
sed -i 's/meshtastic_//g' *
