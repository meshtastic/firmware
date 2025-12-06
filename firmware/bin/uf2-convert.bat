@ECHO OFF
SETLOCAL EnableDelayedExpansion
TITLE Meshtastic uf2-convert

SET "SCRIPT_NAME=%~nx0"
SET "DEBUG=0"
SET "NRF=0"
SET "UF2CONV_CMD=python3 .\bin\uf2conv.py"

GOTO getopts
:help
ECHO.
ECHO Usage: %SCRIPT_NAME% -t [t-echo^|rak4631^|nano-g2-ultra^|wio-tracker-wm1110^|canaryone^|
ECHO                                 heltec-mesh-node-t114^|tracker-t1000-e^|rak_wismeshtap^|rak2560^|
ECHO                                 nrf52_promicro_diy_tcxo]
ECHO.
ECHO Options:
ECHO     -t target        Specify a platformio NRF target to build for. (required)
ECHO.
ECHO Example: %SCRIPT_NAME% -t rak4631
GOTO eof

:version
ECHO %SCRIPT_NAME% [Version 2.6.0]
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
IF /I "%~1"=="-t" SET "TARGETNAME=%~2" & SHIFT
IF /I "%~1"=="--target" SET "TARGETNAME=%~2" & SHIFT
SHIFT
GOTO getopts
:endopts

CALL :LOG_MESSAGE DEBUG "Checking TARGETNAME parameter..."
IF "__!TARGETNAME!__"=="____" (
    CALL :LOG_MESSAGE DEBUG "Missing -t target input."
    GOTO help
)

IF %DEBUG% EQU 1 SET "UF2CONV_CMD=REM python3 .\bin\uf2conv.py"

SET "NRFTARGETS=t-echo rak4631 nano-g2-ultra wio-tracker-wm1110 canaryone heltec-mesh-node-t114 tracker-t1000-e rak_wismeshtap rak2560 nrf52_promicro_diy_tcxo"
FOR %%a IN (%NRFTARGETS%) DO (
    IF /I "%%a"=="!TARGETNAME!" (
        @REM We are working with any of %NRFTARGETS%.
        SET "NRF=1"
        GOTO end_loop_nrf
    )
)
:end_loop_nrf

@REM Building operations.
IF !NRF! EQU 1 (
    CALL :LOG_MESSAGE INFO "Trying to build for !TARGETNAME!..."
    CALL :RUN_UF2CONV !TARGETNAME! || GOTO eof
) ELSE (
    CALL :LOG_MESSAGE WARN "!TARGETNAME! is not supported..."
    GOTO eof
)

CALL :LOG_MESSAGE INFO "Script complete!."


:eof
ENDLOCAL
EXIT /B %ERRORLEVEL%


:RUN_UF2CONV
@REM Subroutine used to run .\bin\uf2conv.py with arguments.
@REM Also handles %ERRORLEVEL%.
@REM CALL :RUN_UF2CONV [target]
@REM.
@REM Example:: CALL :RUN_UF2CONV rak4631
IF %DEBUG% EQU 1 CALL :LOG_MESSAGE DEBUG "About to run command: !UF2CONV_CMD! .\.pio\build\%~1\firmware.hex -c -o .\.pio\build\%~1\firmware.uf2 -f 0xADA52840"
CALL :RESET_ERROR
!UF2CONV_CMD! .\.pio\build\%~1\firmware.hex -c -o .\.pio\build\%~1\firmware.uf2 -f 0xADA52840
IF %ERRORLEVEL% NEQ 0 (
    CALL :LOG_MESSAGE ERROR "Error running command: !UF2CONV_CMD! .\.pio\build\%~1\firmware.hex -c -o .\.pio\build\%~1\firmware.uf2 -f 0xADA52840"
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
