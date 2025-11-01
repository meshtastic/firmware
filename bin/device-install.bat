@ECHO OFF
SETLOCAL EnableDelayedExpansion
TITLE Meshtastic device-install

SET "SCRIPT_NAME=%~nx0"
SET "DEBUG=0"
SET "PYTHON="
SET "ESPTOOL_BAUD=115200"
SET "ESPTOOL_CMD="
SET "LOGCOUNTER=0"
SET "BPS_RESET=0"
@REM Default offsets.
@REM https://github.com/meshtastic/web-flasher/blob/main/stores/firmwareStore.ts#L202
SET "OTA_OFFSET=0x260000"
SET "SPIFFS_OFFSET=0x300000"

GOTO getopts
:help
ECHO Flash image file to device, but first erasing and writing system information.
ECHO.
ECHO Usage: %SCRIPT_NAME% -f filename [-p PORT] [-P python] [--1200bps-reset]
ECHO.
ECHO Options:
ECHO     -f filename      The firmware .factory.bin file to flash.  Custom to your device type and region. (required)
ECHO                      The file must be located in this current directory.
ECHO     -p PORT          Set the environment variable for ESPTOOL_PORT.
ECHO                      If not set, ESPTOOL iterates all ports (Dangerous).
ECHO     -P python        Specify alternate python interpreter to use to invoke esptool. (default: python)
ECHO                      If supplied the script will use python.
ECHO                      If not supplied the script will try to find esptool in Path.
ECHO     --1200bps-reset  Attempt to place the device in correct mode. (1200bps Reset)
ECHO                      Some hardware requires this twice.
ECHO.
ECHO Example: %SCRIPT_NAME% -p COM17 --1200bps-reset
ECHO Example: %SCRIPT_NAME% -f firmware-t-deck-tft-2.6.0.0b106d4.factory.bin -p COM11
ECHO Example: %SCRIPT_NAME% -f firmware-unphone-2.6.0.0b106d4.factory.bin -p COM11
GOTO eof

:version
ECHO %SCRIPT_NAME% [Version 2.7.0]
ECHO Meshtastic
GOTO eof

:getopts
IF "%~1"=="" GOTO endopts
IF /I "%~1"=="-?" GOTO help
IF /I "%~1"=="-h" GOTO help
IF /I "%~1"=="--help" GOTO help
IF /I "%~1"=="-v" GOTO version
IF /I "%~1"=="--version" GOTO version
IF /I "%~1"=="--debug" SET "DEBUG=1" & CALL :LOG_MESSAGE DEBUG "DEBUG mode: enabled."
IF /I "%~1"=="-f" SET "FILENAME=%~2" & SHIFT
IF "%~1"=="-p" SET "ESPTOOL_PORT=%~2" & SHIFT
IF /I "%~1"=="--port" SET "ESPTOOL_PORT=%~2" & SHIFT
IF "%~1"=="-P" SET "PYTHON=%~2" & SHIFT
IF /I "%~1"=="--1200bps-reset" SET "BPS_RESET=1"
SHIFT
GOTO getopts
:endopts

IF %BPS_RESET% EQU 1 GOTO skip-filename

CALL :LOG_MESSAGE DEBUG "Checking FILENAME parameter..."
IF "__!FILENAME!__"=="____" (
    CALL :LOG_MESSAGE DEBUG "Missing -f filename input."
    GOTO help
) ELSE (
    CALL :LOG_MESSAGE DEBUG "Filename: !FILENAME!"
    IF NOT "__!FILENAME: =!__"=="__!FILENAME!__" (
        CALL :LOG_MESSAGE ERROR "Filename containing spaces are not supported."
        GOTO help
    )
    IF NOT "__!FILENAME:.factory.bin=!__"=="__!FILENAME!__" (
        CALL :LOG_MESSAGE ERROR "Filename must be a firmware-*.factory.bin file."
        GOTO help
    )
    @REM Remove ".\" or "./" file prefix if present.
    SET "FILENAME=!FILENAME:.\=!"
    SET "FILENAME=!FILENAME:./=!"
)

CALL :LOG_MESSAGE DEBUG "Checking if !FILENAME! exists..."
IF NOT EXIST !FILENAME! (
    CALL :LOG_MESSAGE ERROR "File does not exist: !FILENAME!. Terminating."
    GOTO eof
)

CALL :LOG_MESSAGE DEBUG "Checking for metadata..."
@REM Derive metadata filename from firmware filename.
SET "METAFILE=!FILENAME:.factory.bin=!.mt.json"
IF EXIST !METAFILE! (
    @REM Print parsed json with powershell
    CALL :LOG_MESSAGE INFO "Firmware metadata: !METAFILE!"
    powershell -NoProfile -Command "(Get-Content '!METAFILE!' | ConvertFrom-Json | Out-String).Trim()"

    @REM Save metadata values to variables for later use.
    FOR /f "usebackq" %%A IN (`powershell -NoProfile -Command ^
        "(Get-Content '!METAFILE!' | ConvertFrom-Json).mcu"`) DO SET "MCU=%%A"
    FOR /f "usebackq" %%A IN (`powershell -NoProfile -Command ^
        "(Get-Content '!METAFILE!' | ConvertFrom-Json).part | Where-Object { $_.subtype -eq 'ota_1' } | Select-Object -ExpandProperty offset"`
    ) DO SET "OTA_OFFSET=%%A"
    FOR /f "usebackq" %%A IN (`powershell -NoProfile -Command ^
        "(Get-Content '!METAFILE!' | ConvertFrom-Json).part | Where-Object { $_.subtype -eq 'spiffs' } | Select-Object -ExpandProperty offset"`
    ) DO SET "SPIFFS_OFFSET=%%A"
) ELSE (
    CALL :LOG_MESSAGE ERROR "No metadata file found: !METAFILE!"
    GOTO eof
)

:skip-filename

CALL :LOG_MESSAGE DEBUG "Determine the correct esptool command to use..."
IF NOT "__%PYTHON%__"=="____" (
    SET "ESPTOOL_CMD=!PYTHON! -m esptool"
    CALL :LOG_MESSAGE DEBUG "Python interpreter supplied."
) ELSE (
    CALL :LOG_MESSAGE DEBUG "Python interpreter NOT supplied. Looking for esptool..."
    WHERE esptool >nul 2>&1
    IF %ERRORLEVEL% EQU 0 (
        @REM WHERE exits with code 0 if esptool is found.
        SET "ESPTOOL_CMD=esptool"
    ) ELSE (
        SET "ESPTOOL_CMD=python -m esptool"
        CALL :RESET_ERROR
    )
)

CALL :LOG_MESSAGE DEBUG "Checking esptool command !ESPTOOL_CMD!..."
!ESPTOOL_CMD! >nul 2>&1
IF %ERRORLEVEL% EQU 9009 (
    @REM 9009 = command not found on Windows
    CALL :LOG_MESSAGE ERROR "esptool not found: !ESPTOOL_CMD!"
    EXIT /B 1
)
IF %DEBUG% EQU 1 (
    CALL :LOG_MESSAGE DEBUG "Skipping ESPTOOL_CMD steps."
    SET "ESPTOOL_CMD=REM !ESPTOOL_CMD!"
)

CALL :LOG_MESSAGE DEBUG "Using esptool command: !ESPTOOL_CMD!"
IF "__!ESPTOOL_PORT!__" == "____" (
    CALL :LOG_MESSAGE WARN "Using esptool port: UNSET."
) ELSE (
    SET "ESPTOOL_CMD=!ESPTOOL_CMD! --port !ESPTOOL_PORT!"
    CALL :LOG_MESSAGE INFO "Using esptool port: !ESPTOOL_PORT!."
)
CALL :LOG_MESSAGE INFO "Using esptool baud: !ESPTOOL_BAUD!."

IF %BPS_RESET% EQU 1 (
    @REM Attempt to change mode via 1200bps Reset.
    CALL :RUN_ESPTOOL 1200 --after no_reset read_flash_status
    GOTO eof
)

@REM Extract PROGNAME from %FILENAME% for later use.
SET "PROGNAME=!FILENAME:.factory.bin=!"
CALL :LOG_MESSAGE DEBUG "Computed PROGNAME: !PROGNAME!"

IF "__!MCU!__" == "__esp32s3__" (
    @REM We are working with ESP32-S3
    SET "OTA_FILENAME=bleota-s3.bin"
) ELSE IF "__!MCU!__" == "__esp32c3__" (
    @REM We are working with ESP32-C3
    SET "OTA_FILENAME=bleota-c3.bin"
) ELSE (
    @REM Everything else
    SET "OTA_FILENAME=bleota.bin"
)
CALL :LOG_MESSAGE DEBUG "Set OTA_FILENAME to: !OTA_FILENAME!"

@REM Set SPIFFS filename with "littlefs-" prefix.
SET "SPIFFS_FILENAME=littlefs-!PROGNAME:firmware-=!.bin"
CALL :LOG_MESSAGE DEBUG "Set SPIFFS_FILENAME to: !SPIFFS_FILENAME!"

CALL :LOG_MESSAGE DEBUG "Set OTA_OFFSET to: !OTA_OFFSET!"
CALL :LOG_MESSAGE DEBUG "Set SPIFFS_OFFSET to: !SPIFFS_OFFSET!"

@REM Ensure target files exist before flashing operations.
IF NOT EXIST !FILENAME! CALL :LOG_MESSAGE ERROR "File does not exist: "!FILENAME!". Terminating." & EXIT /B 2 & GOTO eof
IF NOT EXIST !OTA_FILENAME! CALL :LOG_MESSAGE ERROR "File does not exist: "!OTA_FILENAME!". Terminating." & EXIT /B 2 & GOTO eof
IF NOT EXIST !SPIFFS_FILENAME! CALL :LOG_MESSAGE ERROR "File does not exist: "!SPIFFS_FILENAME!". Terminating." & EXIT /B 2 & GOTO eof

@REM Flashing operations.
CALL :LOG_MESSAGE INFO "Trying to flash "!FILENAME!", but first erasing and writing system information..."
CALL :RUN_ESPTOOL !ESPTOOL_BAUD! erase_flash || GOTO eof
CALL :RUN_ESPTOOL !ESPTOOL_BAUD! write_flash 0x00 "!FILENAME!" || GOTO eof

CALL :LOG_MESSAGE INFO "Trying to flash BLEOTA "!OTA_FILENAME!" at OTA_OFFSET !OTA_OFFSET!..."
CALL :RUN_ESPTOOL !ESPTOOL_BAUD! write_flash !OTA_OFFSET! "!OTA_FILENAME!" || GOTO eof

CALL :LOG_MESSAGE INFO "Trying to flash SPIFFS "!SPIFFS_FILENAME!" at SPIFFS_OFFSET !SPIFFS_OFFSET!..."
CALL :RUN_ESPTOOL !ESPTOOL_BAUD! write_flash !SPIFFS_OFFSET! "!SPIFFS_FILENAME!" || GOTO eof

CALL :LOG_MESSAGE INFO "Script complete!."

:eof
ENDLOCAL
EXIT /B %ERRORLEVEL%


:RUN_ESPTOOL
@REM Subroutine used to run ESPTOOL_CMD with arguments.
@REM Also handles %ERRORLEVEL%.
@REM CALL :RUN_ESPTOOL [Baud] [erase_flash|write_flash] [OFFSET] [Filename]
@REM.
@REM Example:: CALL :RUN_ESPTOOL 115200 write_flash 0x10000 "firmwarefile.bin"
IF %DEBUG% EQU 1 CALL :LOG_MESSAGE DEBUG "About to run command: !ESPTOOL_CMD! --baud %~1 %~2 %~3 %~4"
CALL :RESET_ERROR
!ESPTOOL_CMD! --baud %~1 %~2 %~3 %~4
IF %BPS_RESET% EQU 1 GOTO :eof
IF %ERRORLEVEL% NEQ 0 (
    CALL :LOG_MESSAGE ERROR "Error running command: !ESPTOOL_CMD! --baud %~1 %~2 %~3 %~4"
    EXIT /B %ERRORLEVEL%
)
GOTO :eof

:LOG_MESSAGE
@REM Subroutine used to print log messages in four different levels.
@REM DEBUG messages only get printed if [-d] flag is passed to script.
@REM CALL :LOG_MESSAGE [ERROR|INFO|WARN|DEBUG] "Message"
@REM.
@REM Example:: CALL :LOG_MESSAGE INFO "Message."
SET /A LOGCOUNTER=LOGCOUNTER+1
IF "%1" == "ERROR" CALL :GET_TIMESTAMP & ECHO [91m%1 [0m[37m^| !TIMESTAMP! !LOGCOUNTER! [0m[91m%~2[0m
IF "%1" == "INFO" CALL :GET_TIMESTAMP & ECHO [32m%1  [0m[37m^| !TIMESTAMP! !LOGCOUNTER! [0m[32m%~2[0m
IF "%1" == "WARN" CALL :GET_TIMESTAMP & ECHO [33m%1  [0m[37m^| !TIMESTAMP! !LOGCOUNTER! [0m[33m%~2[0m
IF "%1" == "DEBUG" IF %DEBUG% EQU 1 CALL :GET_TIMESTAMP & ECHO [34m%1 [0m[37m^| !TIMESTAMP! !LOGCOUNTER! [0m[34m%~2[0m
GOTO :eof

:GET_TIMESTAMP
@REM Subroutine used to set !TIMESTAMP! to HH:MM:ss.
@REM CALL :GET_TIMESTAMP
@REM.
@REM Updates: !TIMESTAMP!
FOR /F "tokens=1,2,3 delims=:,." %%a IN ("%TIME%") DO (
    SET "HH=%%a"
    SET "MM=%%b"
    SET "ss=%%c"
)
SET "TIMESTAMP=!HH!:!MM!:!ss!"
GOTO :eof

:RESET_ERROR
@REM Subroutine to reset %ERRORLEVEL% to 0.
@REM CALL :RESET_ERROR
@REM.
@REM Updates: %ERRORLEVEL%
EXIT /B 0
GOTO :eof
