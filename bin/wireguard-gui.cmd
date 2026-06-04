@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
set "LOCAL_PY=%REPO_ROOT%\.wireguard-gui-venv\Scripts\pythonw.exe"
set "TEMP_PY=%TEMP%\meshtastic-wg-venv\Scripts\pythonw.exe"

if exist "%LOCAL_PY%" (
    start "" "%LOCAL_PY%" "%SCRIPT_DIR%wireguard-gui.py"
    exit /b 0
)

if exist "%TEMP_PY%" (
    start "" "%TEMP_PY%" "%SCRIPT_DIR%wireguard-gui.py"
    exit /b 0
)

where pythonw.exe >nul 2>nul
if %ERRORLEVEL% equ 0 (
    start "" pythonw.exe "%SCRIPT_DIR%wireguard-gui.py"
    exit /b 0
)

where python.exe >nul 2>nul
if %ERRORLEVEL% equ 0 (
    start "" python.exe "%SCRIPT_DIR%wireguard-gui.py"
    exit /b 0
)

echo Python was not found. Install Python, then run bin\setup-wireguard-gui.cmd.
pause
exit /b 1
