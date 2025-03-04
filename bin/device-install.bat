@ECHO OFF
SETLOCAL EnableDelayedExpansion
set "SCRIPTNAME=%~nx0"
set "PYTHON=python"
set "WEB_APP=0"
set "TFT8=0"
set "TFT16=0"
SET "TFT_BUILD=0"
SET "DO_SPECIAL_OTA=0"

:: Determine the correct esptool command to use
where esptool >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "ESPTOOL_CMD=esptool"
) else (
    set "ESPTOOL_CMD=%PYTHON% -m esptool"
)

goto GETOPTS
:HELP
echo Usage: %SCRIPTNAME% [-h] [-p ESPTOOL_PORT] [-P PYTHON] [-f FILENAME] [--web] [--tft] [--tft-16mb]
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
if /I "%~1"=="-h" goto HELP & exit /b
if /I "%~1"=="--help" goto HELP & exit /b
if "%~1"=="-p" set "ESPTOOL_PORT=%~2" & SHIFT & SHIFT & goto GETOPTS
if "%~1"=="-P" set "PYTHON=%~2" & SHIFT & SHIFT & goto GETOPTS
if /I "%~1"=="-f" set "FILENAME=%~2" & SHIFT & SHIFT & goto GETOPTS
if /I "%~1"=="--web" set "WEB_APP=1" & SHIFT & goto GETOPTS
if /I "%~1"=="--tft" set "TFT8=1" & SHIFT & goto GETOPTS
if /I "%~1"=="--tft-16mb" set "TFT16=1" & SHIFT & goto GETOPTS
SHIFT
IF NOT "%~1"=="" goto GETOPTS

IF "__%FILENAME%__" == "____" (
    echo "Missing FILENAME"
    goto HELP
)

:: Check if FILENAME contains "-tft-" and either TFT8 or TFT16 is 1 (--tft, -tft-16mb)
IF NOT "%FILENAME:-tft-=%"=="%FILENAME%" (
    SET "TFT_BUILD=1"
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
    SET "OTA_OFFSET=0x260000"

    @REM littlefs* offset for MUI 8mb (--tft) and OTA OFFSET.
    IF "%TFT8%"=="1" IF "%TFT_BUILD%"=="1" (
        SET "OFFSET=0x670000"
        SET "OTA_OFFSET=0x340000"
    ) else (
        echo Ignoring --tft, not a TFT Build.
    )

    @REM littlefs* offset for MUI 16mb (--tft-16mb) and OTA OFFSET.
    IF "%TFT16%"=="1" IF "%TFT_BUILD%"=="1" (
        SET "OFFSET=0xc90000"
        SET "OTA_OFFSET=0x650000"
    ) else (
        echo Ignoring --tft-16mb, not a TFT Build.
    )

    echo Trying to flash update %FILENAME%, but first erasing and writing system information"
    %ESPTOOL_CMD% --baud 115200 erase_flash
    %ESPTOOL_CMD% --baud 115200 write_flash 0x00 "%FILENAME%"

    @REM Account for S3 and C3 board's different OTA partition
    IF NOT "%FILENAME%"=="%FILENAME:s3=%" SET "DO_SPECIAL_OTA=1"
    IF NOT "%FILENAME%"=="%FILENAME:v3=%" SET "DO_SPECIAL_OTA=1"
    IF NOT "%FILENAME%"=="%FILENAME:t-deck=%" SET "DO_SPECIAL_OTA=1"
    IF NOT "%FILENAME%"=="%FILENAME:wireless-paper=%" SET "DO_SPECIAL_OTA=1"
    IF NOT "%FILENAME%"=="%FILENAME:wireless-tracker=%" SET "DO_SPECIAL_OTA=1"
    IF NOT "%FILENAME%"=="%FILENAME:station-g2=%" SET "DO_SPECIAL_OTA=1"
    IF NOT "%FILENAME%"=="%FILENAME:unphone=%" SET "DO_SPECIAL_OTA=1"
    IF NOT "%FILENAME%"=="%FILENAME:esp32c3=%" SET "DO_SPECIAL_OTA=1"

    IF "!DO_SPECIAL_OTA!"=="1" (
        IF NOT "%FILENAME%"=="%FILENAME:esp32c3=%" (
            %ESPTOOL_CMD% --baud 115200 write_flash !OTA_OFFSET! bleota-c3.bin
        ) ELSE (
            %ESPTOOL_CMD% --baud 115200 write_flash !OTA_OFFSET! bleota-s3.bin
        )
    ) ELSE (
        %ESPTOOL_CMD% --baud 115200 write_flash !OTA_OFFSET! bleota.bin
    )

    @REM Check if WEB_APP (--web) is enabled and add "littlefswebui-" to BASENAME else "littlefs-".
    IF "%WEB_APP%"=="1" (
        @REM Check it the file exist before trying to write it.
        IF EXIST "littlefswebui-%BASENAME%" (
            %ESPTOOL_CMD% --baud 115200 write_flash !OFFSET! "littlefswebui-%BASENAME%"
        ) else (
            echo Error: file "littlefswebui-%BASENAME%" wasn't found, littlefswebui not written.
            goto EOF
        )
    ) else (
        @REM Check it the file exist before trying to write it.
        IF EXIST "littlefs-%BASENAME%" (
            %ESPTOOL_CMD% --baud 115200 write_flash !OFFSET! "littlefs-%BASENAME%"
        ) else (
            echo Error: file "littlefs-%BASENAME%" wasn't found, littlefs not written.
            goto EOF
        )
    )
) else (
    echo "Invalid file: %FILENAME%"
    goto HELP
)

:EOF
@REM Cleanup vars.
SET "SCRIPTNAME="
SET "PYTHON="
SET "WEB_APP="
SET "TFT8="
Set "TFT16="
SET "OFFSET="
SET "OTA_OFFSET="
SET "DO_SPECIAL_OTA="
SET "FILENAME="
SET "BASENAME="
endlocal
exit /b 0