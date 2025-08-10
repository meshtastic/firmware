@echo off
REM Windows batch script to build Meshtastic firmware using Docker

echo Building Meshtastic Elecrow firmware using Docker...
echo.

REM Create output directory if it doesn't exist
if not exist "firmware-output" mkdir firmware-output

REM Build the Docker image
echo Building Docker image...
docker build -f Dockerfile.esp32 -t meshtastic-esp32-builder .
if %ERRORLEVEL% neq 0 (
    echo Failed to build Docker image!
    exit /b 1
)

echo.
echo Running Docker container to build firmware...

REM Run the container and copy output
docker run --rm -v "%cd%\firmware-output:/host-output" meshtastic-esp32-builder sh -c "cp -r /output/* /host-output/ && echo 'Build artifacts copied to host' && ls -la /host-output/"

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo.
echo Firmware files are available in: firmware-output\
echo.
dir firmware-output
echo ========================================
echo.