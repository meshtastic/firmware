cd protobufs && ..\nanopb-0.4.9\generator-bin\protoc.exe --experimental_allow_proto3_optional "--nanopb_out=-S.cpp -v:..\src\mesh\generated" -I=..\protobufs\ ..\protobufs\meshtastic\*.proto
