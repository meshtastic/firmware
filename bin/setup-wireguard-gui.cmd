@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."

where python.exe >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo Python was not found. Install Python 3, then retry.
    pause
    exit /b 1
)

pushd "%REPO_ROOT%"
python.exe "%SCRIPT_DIR%setup-wireguard-gui.py" %*
set "RESULT=%ERRORLEVEL%"
popd

if %RESULT% neq 0 (
    echo.
    echo Setup failed.
    pause
    exit /b %RESULT%
)

echo.
echo Setup complete. You can now run bin\wireguard-gui.cmd or use your desktop shortcut.
pause
exit /b 0
