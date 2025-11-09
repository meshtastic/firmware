@ECHO OFF
SETLOCAL

echo Regenerating protobuf artifacts via protobufs\scripts\build.bat.
echo Ensure required commands are available: node, npx, git.
echo Artifacts will be produced under protobufs\build.
echo

cd protobufs
.\scripts\build.bat
xcopy /Y /S /I build\c\* ..\src\mesh\generated\
GOTO eof

:eof
ENDLOCAL
EXIT /B %ERRORLEVEL%
