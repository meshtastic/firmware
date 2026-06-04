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
python.exe "%SCRIPT_DIR%build-wireguard-gui-exe.py" %*
set "RESULT=%ERRORLEVEL%"
popd

if %RESULT% neq 0 (
    echo.
    echo Build failed.
    pause
    exit /b %RESULT%
)

echo.
echo Build complete. The EXE is in dist\.
pause
exit /b 0
