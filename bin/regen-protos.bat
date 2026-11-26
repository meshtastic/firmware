@ECHO OFF
SETLOCAL

SET "FIELDMETA=%TEMP%\protoc-gen-fieldmeta.exe"
go -C protobufs\tools\protoc-gen-fieldmeta build -o "%FIELDMETA%" . || EXIT /B 1

cd protobufs
..\nanopb-0.4.9\generator-bin\protoc.exe --experimental_allow_proto3_optional "--plugin=protoc-gen-fieldmeta=%FIELDMETA%" "--nanopb_out=-S.cpp -v:..\src\mesh\generated" "--fieldmeta_out=target=c,attributes=sensitive:..\src\mesh\generated" -I=..\protobufs\ ..\protobufs\meshtastic\*.proto
GOTO eof

:eof
ENDLOCAL
EXIT /B %ERRORLEVEL%
