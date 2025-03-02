@ECHO OFF
SETLOCAL EnableDelayedExpansion

set PYTHON=python
set WEB_APP=0
set TFT8=0
set TFT16=0

:: Determine the correct esptool command to use
where esptool >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "ESPTOOL_CMD=esptool"
) else (
    set "ESPTOOL_CMD=%PYTHON% -m esptool"
)

goto GETOPTS
:HELP
echo Usage: %~nx0 [-h] [-p ESPTOOL_PORT] [-P PYTHON] [-f FILENAME^|FILENAME] [--web] [--tft] [--tft-16mb]
echo Flash image file to device, but first erasing and writing system information
echo.
echo     -h               Display this help and exit
echo     -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerrous).
echo     -P PYTHON        Specify alternate python interpreter to use to invoke esptool. (Default: %PYTHON%)
echo     -f FILENAME      The .bin file to flash.  Custom to your device type and region.
echo     --web            Flash WEB APP.
echo     --tft            Flash MUI 8mb
echo     --tft-16mb       Flash MUI 16mb
goto EOF

:GETOPTS
if /I "%1"=="-h" goto HELP
if /I "%1"=="--help" goto HELP
if /I "%1"=="-F" set "FILENAME=%2" & SHIFT
if /I "%1"=="-p" set ESPTOOL_PORT=%2 & SHIFT
if /I "%1"=="-P" set PYTHON=%2 & SHIFT
if /I "%1"=="--web" set "WEB_APP=1" & SHIFT
if /I "%1"=="--tft" set "TFT8=1" & SHIFT
if /I "%1"=="--tft-16mb" set "TFT16=1" & SHIFT
SHIFT
IF NOT "__%1__"=="____" goto GETOPTS

IF "__%FILENAME%__" == "____" (
    echo "Missing FILENAME"
    goto HELP
)

:: Check if FILENAME contains "-tft-" and either TFT8 or TFT16 is 1 (--tft, -tft-16mb)
IF NOT "%FILENAME:-tft-=%"=="%FILENAME%" (
    IF NOT "%TFT8%"=="1" IF NOT "%TFT16%"=="1" (
        echo Error: Either --tft or --tft-16mb must be set to use a TFT build.
        goto EOF
    )
    IF "%TFT8%"=="1" IF "%TFT16%"=="1" (
        echo Error: Both --tft and --tft-16mb must NOT be set at the same time.
        goto EOF
    )
)

:: Extract BASENAME from %FILENAME% for later use.
SET BASENAME=%FILENAME:firmware-=%

IF EXIST %FILENAME% IF x%FILENAME:update=%==x%FILENAME% (
    @REM Default littlefs* offset (--web).
    SET "OFFSET=0x300000"

  	@REM Default OTA Offset
	OTA_OFFSET=0x260000
    
    @REM littlefs* offset for MUI 8mb (--tft) and OTA OFFSET.
    IF "%TFT8%"=="1" (
        SET "OFFSET=0x670000"
   		SET "OTA_OFFSET=0x340000"
    )

    @REM littlefs* offset for MUI 16mb (--tft-16mb) and OTA OFFSET.
    IF "%TFT16%"=="1" (
        SET "OFFSET=0xc90000"
		SET "OTA_OFFSET=0x650000"
    )

    echo Trying to flash update %FILENAME%, but first erasing and writing system information"
    %ESPTOOL_CMD% --baud 115200 erase_flash
    %ESPTOOL_CMD% --baud 115200 write_flash 0x00 "%FILENAME%"
    
    @REM Account for S3 and C3 board's different OTA partition
    IF x%FILENAME:s3=%==x%FILENAME% IF x%FILENAME:v3=%==x%FILENAME% IF x%FILENAME:t-deck=%==x%FILENAME% IF x%FILENAME:wireless-paper=%==x%FILENAME% IF x%FILENAME:wireless-tracker=%==x%FILENAME% IF x%FILENAME:station-g2=%==x%FILENAME% IF x%FILENAME:unphone=%==x%FILENAME% (
        IF x%FILENAME:esp32c3=%==x%FILENAME% (
            %ESPTOOL_CMD% --baud 115200 write_flash !OTA_OFFSET! bleota.bin
        ) else (
            %ESPTOOL_CMD% --baud 115200 write_flash !OTA_OFFSET! bleota-c3.bin
        )
    ) else (
        echo %ESPTOOL_CMD% --baud 115200 write_flash !OTA_OFFSET! bleota-s3.bin
    )

    @REM Check if WEB_APP (--web) is enabled and add "littlefswebui-" to BASENAME else "littlefs-".
    IF "%WEB_APP%"=="1" (
        @REM Check it the file exist before trying to write it.
        IF EXIST "littlefswebui-%BASENAME%" (
            %ESPTOOL_CMD% --baud 115200 write_flash !OFFSET! "littlefswebui-%BASENAME%"
        )
    ) else (
        @REM Check it the file exist before trying to write it.
        IF EXIST "littlefs-%BASENAME%" (
            %ESPTOOL_CMD% --baud 115200 write_flash !OFFSET! "littlefs-%BASENAME%"
        )
    )
) else (
    echo "Invalid file: %FILENAME%"
    goto HELP
) else (
    echo "Invalid file: %FILENAME%"
    goto HELP
)

:EOF
endlocal
exit /b 0