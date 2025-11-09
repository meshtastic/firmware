@ECHO OFF
SETLOCAL

echo Regenerating protobuf artifacts via protobufs\scripts\build.bat.
echo Ensure required commands are available: node, npx, git.
echo Artifacts will be produced under protobufs\build.
echo

cd protobufs
.\scripts\build.bat

rem clean target directory
rmdir /S /Q ..\src\mesh\generated\meshtastic
rmdir /S /Q ..\src\mesh\generated\validate

rem copy new artifacts to target directory
xcopy /Y /S /I build\c\* ..\src\mesh\generated\

rem delete nanopb generated files because firmware uses a vendored version
del /S /Q ..\src\mesh\generated\nanopb.pb.*

GOTO eof

:eof
ENDLOCAL
EXIT /B %ERRORLEVEL%
