@ECHO OFF
SETLOCAL EnableDelayedExpansion
TITLE Meshtastic device-update

SET "SCRIPT_NAME=%~nx0"
SET "DEBUG=0"
SET "PYTHON="
SET "ESPTOOL_BAUD=115200"
SET "ESPTOOL_CMD="
SET "LOGCOUNTER=0"

GOTO getopts
:help
ECHO Flash image file to device, but leave existing system intact.
ECHO.
ECHO Usage: %SCRIPT_NAME% -f filename [-p PORT] [-P python]
ECHO.
ECHO Options:
ECHO     -f filename      The update .bin file to flash.  Custom to your device type and region. (required)
ECHO                      The file must be located in this current directory.
ECHO     -p PORT          Set the environment variable for ESPTOOL_PORT.
ECHO                      If not set, ESPTOOL iterates all ports (Dangerous).
ECHO     -P python        Specify alternate python interpreter to use to invoke esptool. (default: python)
ECHO                      If supplied the script will use python.
ECHO                      If not supplied the script will try to find esptool in Path.
ECHO.
ECHO Example: %SCRIPT_NAME% -f firmware-t-deck-tft-2.6.0.0b106d4-update.bin -p COM11
GOTO eof

:version
ECHO %SCRIPT_NAME% [Version 2.6.1]
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
SHIFT
GOTO getopts
:endopts

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
    @REM Remove ".\" or "./" file prefix if present.
    SET "FILENAME=!FILENAME:.\=!"
    SET "FILENAME=!FILENAME:./=!"
)

CALL :LOG_MESSAGE DEBUG "Checking if !FILENAME! exists..."
IF NOT EXIST !FILENAME! (
    CALL :LOG_MESSAGE ERROR "File does not exist: !FILENAME!. Terminating."
    GOTO eof
)

IF "!FILENAME:update=!"=="!FILENAME!" (
    CALL :LOG_MESSAGE DEBUG "We are NOT working with a *update* file. !FILENAME!"
    CALL :LOG_MESSAGE INFO "Use script device-install.bat to flash !FILENAME!."
    GOTO eof
) ELSE (
    CALL :LOG_MESSAGE DEBUG "We are working with a *update* file. !FILENAME!"
)

CALL :LOG_MESSAGE DEBUG "Determine the correct esptool command to use..."
IF NOT "__%PYTHON%__"=="____" (
    SET "ESPTOOL_CMD=!PYTHON! -m esptool"
    CALL :LOG_MESSAGE DEBUG "Python interpreter supplied."
) ELSE (
    CALL :LOG_MESSAGE DEBUG "Python interpreter NOT supplied. Looking for esptool...
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
IF %ERRORLEVEL% GEQ 2 (
    @REM esptool exits with code 1 if help is displayed.
    CALL :LOG_MESSAGE ERROR "esptool not found: !ESPTOOL_CMD!"
    EXIT /B 1
    GOTO eof
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

@REM Flashing operations.
CALL :LOG_MESSAGE INFO "Trying to flash update "!FILENAME!" at OFFSET 0x10000..."
CALL :RUN_ESPTOOL !ESPTOOL_BAUD! write_flash 0x10000 "!FILENAME!" || GOTO eof

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
