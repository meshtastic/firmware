Param(
    [string]$Env = "tbeam",
    [string]$Image = "meshtastic-firmware-builder:latest",
    [string]$WorkspaceVolume = "meshtastic_fw_workspace",
    [string]$PioCacheVolume = "meshtastic_pio_cache",
    [string]$PipCacheVolume = "meshtastic_pip_cache",
    [switch]$CleanWorkspace,
    [switch]$NoPull,
    [switch]$RebuildBuilder
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ALPINE_IMAGE = "alpine:3.20"
$CONTAINER_WORKDIR = "/work"

# Cross-platform temp directory root
$TempRoot = [System.IO.Path]::GetTempPath()
if (-not $TempRoot) { $TempRoot = "/tmp" }

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [switch]$IgnoreErrors
    )
    Write-Host "→ $Command" -ForegroundColor Cyan
    & $env:COMSPEC /c $Command | ForEach-Object { $_ }
    if (-not $IgnoreErrors -and $LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $Command"
    }
}

function Invoke-Docker {
    param(
        [Parameter(Mandatory = $true)][string[]]$Args,
        [switch]$IgnoreErrors
    )
    Write-Host ("🐳 docker " + ($Args -join ' ')) -ForegroundColor DarkCyan
    & docker @Args | ForEach-Object { $_ }
    if (-not $IgnoreErrors -and $LASTEXITCODE -ne 0) {
        throw ("docker " + ($Args -join ' ') + " failed with exit code $LASTEXITCODE")
    }
}

function Test-DockerAvailable {
    try {
        Get-Command docker -ErrorAction Stop | Out-Null
    }
    catch {
        throw "Docker CLI not found. Please install Docker Desktop and ensure 'docker' is on PATH."
    }
    Invoke-Docker @("version") | Out-Null
}

function Test-DockerImage {
    param([string]$ImageName)
    if ($NoPull) { return }
    $exists = (& docker images -q --filter "reference=$ImageName" | Select-Object -First 1)
    if (-not $exists) {
        Invoke-Docker @("pull", $ImageName)
    }
}

function Test-DockerVolume {
    param([string]$Name)
    $null = & docker volume inspect $Name 2>$null
    if ($LASTEXITCODE -ne 0) {
        Invoke-Docker @("volume", "create", $Name) | Out-Null
    }
}

function Clear-VolumePath {
    param(
        [string]$VolumeName,
        [string]$PathInVolume
    )
    Invoke-Docker @("pull", $ALPINE_IMAGE) -IgnoreErrors | Out-Null
    $workMount = ("{0}:{1}" -f $VolumeName, $PathInVolume)
    $steps = @(
        "rm -rf ${PathInVolume}/*"
    )
    $cmd = New-BashCommand -Steps $steps
    Invoke-Docker @("run", "--rm", "-v", $workMount, $ALPINE_IMAGE, "sh", "-lc", $cmd)
}

function New-BuilderImageIfMissing {
    param([string]$Tag)
    if ($NoPull -and -not $RebuildBuilder) { return }
    $exists = (& docker images -q --filter "reference=$Tag" | Select-Object -First 1)
    if ($exists -and -not $RebuildBuilder) { return }
    # Build a minimal image with PlatformIO preinstalled to speed up runs
    $tempDir = Join-Path $TempRoot ("meshtastic-builder-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
    $dockerfile = @"
FROM python:3.13-slim
ENV DEBIAN_FRONTEND=noninteractive \
    PIP_ROOT_USER_ACTION=ignore
RUN apt-get update \
 && apt-get install -y --no-install-recommends git bash ca-certificates curl wget g++ zip pkg-config dos2unix \
 && rm -rf /var/lib/apt/lists/*
RUN python -m pip install -U pip \
 && python -m pip install -U platformio
"@
    $dfPath = Join-Path $tempDir "Dockerfile"
    Set-Content -LiteralPath $dfPath -Value $dockerfile -Encoding UTF8
    try {
        Invoke-Docker @("build", "-t", $Tag, $tempDir)
    }
    finally {
        Remove-Item -Recurse -Force $tempDir -ErrorAction SilentlyContinue
    }
}

function Copy-Workspace-To-Volume {
    param(
        [string]$VolumeName,
        [string]$PathInVolume,
        [string]$SourcePath
    )
    $containerName = "meshtastic-fw-sync-" + [guid]::NewGuid().ToString('N')
    Invoke-Docker @("pull", $ALPINE_IMAGE) -IgnoreErrors | Out-Null
    try {
        $workMount = ("{0}:{1}" -f $VolumeName, $PathInVolume)
        Invoke-Docker @("create", "--name", $containerName, "-v", $workMount, $ALPINE_IMAGE) | Out-Null
        # Ensure destination exists and is empty when CleanWorkspace is set
        $prepSteps = if ($CleanWorkspace) {
            @(
                "rm -rf ${PathInVolume}/*",
                "mkdir -p ${PathInVolume}"
            )
        }
        else {
            @(
                "mkdir -p ${PathInVolume}"
            )
        }
        $prepCmd = New-BashCommand -Steps $prepSteps
        Invoke-Docker @("run", "--rm", "-v", $workMount, $ALPINE_IMAGE, "sh", "-lc", $prepCmd)
        $src = (Resolve-Path $SourcePath).Path
        # Use docker cp to copy into the volume-backed path
        $containerDest = ("{0}:{1}" -f $containerName, $PathInVolume)
        # Use a POSIX-style suffix for docker cp so it works on Linux runners
        & docker @("cp", "$src/.", $containerDest)
        if ($LASTEXITCODE -ne 0) { throw "docker cp into volume failed with $LASTEXITCODE" }
    }
    finally {
        Invoke-Docker @("rm", "-f", $containerName) -IgnoreErrors | Out-Null
    }
}

function Copy-From-Volume {
    param(
        [string]$VolumeName,
        [string]$PathInVolume,
        [string]$SourceSubPath,
        [string]$DestinationPath
    )
    New-Item -ItemType Directory -Force -Path $DestinationPath | Out-Null
    $containerName = "meshtastic-fw-copy-" + [guid]::NewGuid().ToString('N')
    Invoke-Docker @("pull", $ALPINE_IMAGE) -IgnoreErrors | Out-Null
    try {
        $workMount = ("{0}:{1}" -f $VolumeName, $PathInVolume)
        Invoke-Docker @("create", "--name", $containerName, "-v", $workMount, $ALPINE_IMAGE) | Out-Null
        $containerSrc = ("{0}:{1}/{2}" -f $containerName, $PathInVolume, $SourceSubPath)
        & docker @("cp", $containerSrc, "$DestinationPath")
        if ($LASTEXITCODE -ne 0) { throw "docker cp from volume failed with $LASTEXITCODE" }
    }
    finally {
        Invoke-Docker @("rm", "-f", $containerName) -IgnoreErrors | Out-Null
    }
}

function New-BashCommand {
    param([string[]]$Steps)
    return ($Steps -join ' && ')
}

# 1) Preconditions
Test-DockerAvailable
New-BuilderImageIfMissing -Tag $Image
Test-DockerImage -ImageName $Image
Test-DockerVolume -Name $WorkspaceVolume
Test-DockerVolume -Name $PioCacheVolume
Test-DockerVolume -Name $PipCacheVolume

$repoRoot = (Resolve-Path ".").Path
$firmwareOut = Join-Path $repoRoot "firmware-builds"

# 2) Sync workspace into native Docker volume (no bind mounts)
Copy-Workspace-To-Volume -VolumeName $WorkspaceVolume -PathInVolume $CONTAINER_WORKDIR -SourcePath $repoRoot

# 3) Build inside container using native volumes only
$buildArgs = @(
    "run", "--rm",
    "-v", ("{0}:{1}" -f $WorkspaceVolume, $CONTAINER_WORKDIR),
    "-v", ("{0}:{1}" -f $PioCacheVolume, "/root/.platformio"),
    "-v", ("{0}:{1}" -f $PipCacheVolume, "/root/.cache/pip"),
    "-w", $CONTAINER_WORKDIR,
    $Image,
    "bash", "-lc",
    (New-BashCommand -Steps @(
        'set -euo pipefail',
        'mkdir -p /root/.platformio',
        'if [ -f .platformio/platformio.ini ]; then cp .platformio/platformio.ini /root/.platformio/platformio.ini; fi',
        'platformio --version',
        'python --version',
        'find bin -maxdepth 1 -type f -name ''*.sh'' -exec dos2unix {} +',
        'chmod +x bin/*.sh || true',
        'VERSION=$(bin/buildinfo.py long || printf dev)',
        'OUTDIR=release',
        'rm -rf $OUTDIR/* || true',
        'mkdir -p $OUTDIR',
        ('platformio pkg install -e {0}' -f $Env),
        ('pio run --environment {0}' -f $Env),
        ('SRCELF=.pio/build/{0}/firmware.elf' -f $Env),
        ('SRCFACT=.pio/build/{0}/firmware.factory.bin' -f $Env),
        ('SRCBIN=.pio/build/{0}/firmware.bin' -f $Env),
        ('SRCHEX=.pio/build/{0}/firmware.hex' -f $Env),
        ('SRCUF2=.pio/build/{0}/firmware.uf2' -f $Env),
        ('if [ -f $SRCELF ]; then cp $SRCELF $OUTDIR/firmware-{0}-$VERSION.elf; fi' -f $Env),
        ('if [ -f $SRCFACT ]; then cp $SRCFACT $OUTDIR/firmware-{0}-$VERSION.bin; fi' -f $Env),
        ('if [ -f $SRCBIN ]; then cp $SRCBIN $OUTDIR/firmware-{0}-$VERSION-update.bin; fi' -f $Env),
        ('if [ -f $SRCHEX ]; then cp $SRCHEX $OUTDIR/firmware-{0}-$VERSION.hex; fi' -f $Env),
        ('if [ -f $SRCUF2 ]; then cp $SRCUF2 $OUTDIR/firmware-{0}-$VERSION.uf2; fi' -f $Env),
        ('if [ -f .pio/build/{0}/firmware.bin ]; then pio run --environment {0} -t buildfs || true; fi' -f $Env),
        ('if [ -f .pio/build/{0}/littlefs.bin ]; then cp .pio/build/{0}/littlefs.bin $OUTDIR/littlefs-{0}-$VERSION.bin; fi' -f $Env)
    ))
)
Invoke-Docker $buildArgs

# 4) Copy results back to host
$tempCopyDir = Join-Path $TempRoot ("meshtastic-release-" + [guid]::NewGuid().ToString('N'))
Copy-From-Volume -VolumeName $WorkspaceVolume -PathInVolume "/work" -SourceSubPath "release/." -DestinationPath $tempCopyDir

# Create firmware-builds and copy common firmware artifacts
New-Item -ItemType Directory -Force -Path $firmwareOut | Out-Null
$artifactPatterns = @('*.bin', '*.hex', '*.uf2', '*.elf', '*.zip')
$filesToCopy = @()
foreach ($pat in $artifactPatterns) {
    $filesToCopy += Get-ChildItem -Path $tempCopyDir -Filter $pat -File
}
foreach ($srcFile in $filesToCopy) {
    Copy-Item -LiteralPath $srcFile.FullName -Destination $firmwareOut -Force
}

# 5) Done – show what we produced
Write-Host """" -NoNewline
Write-Host "Build complete. Firmware files copied to: $firmwareOut" -ForegroundColor Green
$displayFiles = @()
foreach ($f in $filesToCopy) {
    $dst = Join-Path $firmwareOut $f.Name
    if (Test-Path $dst) { $displayFiles += (Get-Item $dst) }
}
if ($displayFiles.Count -eq 0) {
    Write-Host "No firmware artifacts found to copy from: $tempCopyDir" -ForegroundColor Yellow
}
else {
    $displayFiles | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
}


