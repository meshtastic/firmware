# escape=`
FROM mcr.microsoft.com/windows-cssc/python:3.13-servercore-ltsc2025 AS builder
ARG PIO_ENV=native-windows

SHELL ["powershell", "-NoLogo", "-NoProfile", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

# PlatformIO's package and libdeps trees can run past MAX_PATH.
RUN Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -Value 1

# MSYS2, per https://www.msys2.org/docs/ci/#docker.
RUN Invoke-WebRequest -UseBasicParsing -OutFile msys2.exe `
        -Uri 'https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.sfx.exe'; `
    .\msys2.exe -y -oC:\; `
    Remove-Item msys2.exe; `
    function msys() { C:\msys64\usr\bin\bash.exe @('-lc') + @Args; }; `
    msys ' '; `
    msys 'pacman --noconfirm -Syuu'; `
    msys 'pacman --noconfirm -Syuu'; `
    msys 'pacman --noconfirm -Scc'; `
    taskkill /F /FI 'MODULES eq msys-2.0.dll'

# UCRT64 toolchain and libraries, as in .github/workflows/build_windows_bin.yml.
RUN $env:MSYSTEM = 'UCRT64'; `
    C:\msys64\usr\bin\bash.exe -lc 'set -euo pipefail; `
        pacman -S --needed --noconfirm `
            mingw-w64-ucrt-x86_64-gcc `
            mingw-w64-ucrt-x86_64-pkgconf `
            mingw-w64-ucrt-x86_64-yaml-cpp `
            mingw-w64-ucrt-x86_64-libuv `
            mingw-w64-ucrt-x86_64-jsoncpp `
            mingw-w64-ucrt-x86_64-openssl `
            mingw-w64-ucrt-x86_64-curl `
            mingw-w64-ucrt-x86_64-libusb `
            mingw-w64-ucrt-x86_64-cmake `
            mingw-w64-ucrt-x86_64-ninja `
            git; `
        pacman --noconfirm -Scc'; `
    $rc = $LASTEXITCODE; `
    taskkill /F /FI 'MODULES eq msys-2.0.dll' | Out-Null; `
    exit $rc

# argp is not packaged for the mingw environments, so argp-standalone is built from source
# (no install() rules).
RUN $env:MSYSTEM = 'UCRT64'; `
    C:\msys64\usr\bin\bash.exe -lc 'set -euo pipefail; `
        git clone --depth 1 https://github.com/tom42/argp-standalone /tmp/argp; `
        cd /tmp/argp; `
        cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release .; `
        cmake --build build; `
        cp include/argp-standalone/argp.h /ucrt64/include/argp.h; `
        cp build/src/libargp-standalone.a /ucrt64/lib/libargp.a; `
        rm -rf /tmp/argp'; `
    $rc = $LASTEXITCODE; `
    taskkill /F /FI 'MODULES eq msys-2.0.dll' | Out-Null; `
    exit $rc

# PlatformIO stays on this image's CPython; MSYS2's Python reports a platform tag no wheel matches.
# trunk-ignore(hadolint/DL3013): Do not pin pip package versions
RUN python -m pip install --no-cache-dir platformio

WORKDIR C:\firmware
COPY . C:\firmware

# UCRT64 is reached through PATH. usr\bin goes last, only so bin/readprops.py finds git for
# the version string; safe.directory because the copied tree is not owned by this user.
RUN $env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH + ';C:\msys64\usr\bin'; `
    $env:GIT_CONFIG_COUNT = '1'; `
    $env:GIT_CONFIG_KEY_0 = 'safe.directory'; `
    $env:GIT_CONFIG_VALUE_0 = '*'; `
    python -m platformio run -e $env:PIO_ENV; `
    if ($LASTEXITCODE) { exit $LASTEXITCODE }; `
    New-Item -ItemType Directory -Force release | Out-Null; `
    Copy-Item .pio\build\$env:PIO_ENV\meshtasticd.exe release\meshtasticd.exe

##### PRODUCTION BUILD #############

FROM mcr.microsoft.com/windows/servercore:ltsc2025
LABEL org.opencontainers.image.title="Meshtastic" `
      org.opencontainers.image.description="Windows Meshtastic daemon" `
      org.opencontainers.image.url="https://meshtastic.org" `
      org.opencontainers.image.documentation="https://meshtastic.org/docs/" `
      org.opencontainers.image.authors="Meshtastic" `
      org.opencontainers.image.licenses="GPL-3.0-or-later" `
      org.opencontainers.image.source="https://github.com/meshtastic/firmware/"

SHELL ["powershell", "-NoLogo", "-NoProfile", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

# Config and data paths match the MSI (packaging/windows/meshtasticd.wxs).
COPY --from=builder C:\firmware\release\meshtasticd.exe C:\Meshtastic\meshtasticd.exe
COPY bin\config-dist.yaml C:\ProgramData\Meshtastic\config.yaml

# The binary links statically; running it here proves it needs nothing beyond servercore's DLLs.
# The container has no hardware access to guard, so the node runs unprivileged.
RUN C:\Meshtastic\meshtasticd.exe --version; `
    if ($LASTEXITCODE) { exit $LASTEXITCODE }; `
    New-Item -ItemType Directory -Force C:\ProgramData\Meshtastic\data | Out-Null; `
    icacls C:\ProgramData\Meshtastic\data /grant 'ContainerUser:(OI)(CI)M'; `
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
# trunk-ignore(hadolint/DL3066): Windows accounts are named, not numeric
USER ContainerUser

WORKDIR C:\ProgramData\Meshtastic
VOLUME C:\ProgramData\Meshtastic\data

# Expose Meshtastic TCP API port from the host
EXPOSE 4403

CMD ["C:\\Meshtastic\\meshtasticd.exe", "-c", "C:\\ProgramData\\Meshtastic\\config.yaml", "-d", "C:\\ProgramData\\Meshtastic\\data"]

HEALTHCHECK NONE
