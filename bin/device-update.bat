@ECHO OFF

set PYTHON=python

goto GETOPTS
:HELP
echo Usage: %~nx0 [-h] [-p ESPTOOL_PORT] [-P PYTHON] [-f FILENAME^|FILENAME]
echo Flash image file to device, leave existing system intact.
echo.
echo     -h               Display this help and exit
echo     -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerrous).
echo     -P PYTHON        Specify alternate python interpreter to use to invoke esptool. (Default: %PYTHON%)
echo     -f FILENAME      The *update.bin file to flash.  Custom to your device type.
goto EOF

:GETOPTS
if /I "%1"=="-h" goto HELP
if /I "%1"=="--help" goto HELP
if /I "%1"=="-F" set "FILENAME=%2" & SHIFT
if /I "%1"=="-p" set ESPTOOL_PORT=%2 & SHIFT
if /I "%1"=="-P" set PYTHON=%2 & SHIFT
SHIFT
IF NOT "__%1__"=="____" goto GETOPTS

IF "__%FILENAME%__" == "____" (
    echo "Missing FILENAME"
	goto HELP
)
IF EXIST %FILENAME% IF NOT x%FILENAME:update=%==x%FILENAME% (
    echo Trying to flash update %FILENAME%
    %PYTHON% -m esptool --baud 115200 write_flash 0x10000 %FILENAME%
) else (
    echo "Invalid file: %FILENAME%"
	goto HELP
) else (
    echo "Invalid file: %FILENAME%"
	goto HELP
)

:EOF
