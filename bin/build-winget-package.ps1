#!/usr/bin/env pwsh
#Requires -Version 7.0

<#
.SYNOPSIS
    Builds the meshtasticd MSI and the winget manifests that install it.

.DESCRIPTION
    Writes the MSI and the three winget manifests into -OutputDirectory. The installer manifest
    pins the MSI's SHA256, so the file must be published unchanged at -InstallerUrl.

    WiX is required, pinned below v6, which refuses to build without accepting the OSMF EULA:

        dotnet tool install --global wix --version 5.0.2
        wix extension add -g WixToolset.Util.wixext/5.0.2

.EXAMPLE
    platformio run -e native-windows
    ./bin/build-winget-package.ps1 -Validate

.EXAMPLE
    ./bin/build-winget-package.ps1 -Version 2.8.0 -ReleaseTag v2.8.0.1e982fa
#>

[CmdletBinding()]
param(
    [string]$BinaryPath,
    [string]$SampleConfigPath,
    # Generate manifests for an MSI that already exists (a published release asset) instead of
    # building one. WiX is not needed in that mode.
    [string]$MsiPath,
    [string]$Version,
    [string]$ReleaseTag,
    [string]$OutputDirectory,
    [string]$PackageIdentifier = 'Meshtastic.Meshtasticd',
    [ValidateSet('x64', 'arm64')]
    [string]$Architecture = 'x64',
    [string]$InstallerUrl,
    [string]$ManifestVersion = '1.6.0',
    [switch]$Validate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$WxsPath = Join-Path $RepoRoot 'packaging/windows/meshtasticd.wxs'

# Same UpgradeCode as the .wxs; also the namespace for the per-version ProductCode below.
$UpgradeCode = '8f2c6d41-5b3a-4c8e-9f27-1d0a6b4e73c5'

function Get-RepoVersion {
    param([string]$PropertiesPath)

    $text = Get-Content -LiteralPath $PropertiesPath -Raw
    $fields = foreach ($name in 'major', 'minor', 'build') {
        $match = [regex]::Match($text, "(?m)^\s*$name\s*=\s*(\d+)\s*$")
        if (-not $match.Success) {
            throw "Could not read '$name' from $PropertiesPath"
        }
        $match.Groups[1].Value
    }
    $short = $fields -join '.'

    # Mirrors bin/readprops.py: the release tag carries a 7-char SHA, the package version does not.
    # Empty on a failed or absent git, in which case the tag falls back to the bare version.
    $sha = ''
    try {
        $sha = (git -C $RepoRoot rev-parse --short=7 HEAD 2>$null | Select-Object -First 1)
    } catch {
        $sha = ''
    }

    [pscustomobject]@{
        Short = $short
        Long  = if ($sha) { "$short.$($sha.Trim())" } else { $short }
    }
}

function New-DeterministicGuid {
    param(
        [Parameter(Mandatory)][string]$Namespace,
        [Parameter(Mandatory)][string]$Name
    )

    # RFC 4122 name-based (v5) UUID. Windows Installer wants a fresh ProductCode per version for
    # major upgrades to work, and deriving it keeps that reproducible across rebuilds.
    $bytes = ([guid]$Namespace).ToByteArray()
    # .NET lays the first three fields out little-endian; RFC 4122 hashes them big-endian.
    [array]::Reverse($bytes, 0, 4)
    [array]::Reverse($bytes, 4, 2)
    [array]::Reverse($bytes, 6, 2)

    $hash = [System.Security.Cryptography.SHA1]::HashData($bytes + [System.Text.Encoding]::UTF8.GetBytes($Name))
    $guid = $hash[0..15]
    $guid[6] = ($guid[6] -band 0x0F) -bor 0x50
    $guid[8] = ($guid[8] -band 0x3F) -bor 0x80

    [array]::Reverse($guid, 0, 4)
    [array]::Reverse($guid, 4, 2)
    [array]::Reverse($guid, 6, 2)
    [guid][byte[]]$guid
}

function Get-WixCommand {
    $wix = Get-Command wix -ErrorAction SilentlyContinue
    if (-not $wix) {
        $candidate = Join-Path $env:USERPROFILE '.dotnet/tools/wix.exe'
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
        throw "wix not found. Install it with: dotnet tool install --global wix --version 5.0.2"
    }
    $wix.Source
}

$buildMsi = -not $MsiPath
if ($buildMsi) {
    if (-not $BinaryPath) {
        $BinaryPath = Join-Path $RepoRoot '.pio/build/native-windows/meshtasticd.exe'
    }
    if (-not (Test-Path -LiteralPath $BinaryPath)) {
        throw "meshtasticd.exe not found at '$BinaryPath'. Build it with 'platformio run -e native-windows' or pass -BinaryPath."
    }
    $BinaryPath = (Resolve-Path -LiteralPath $BinaryPath).Path

    if (-not $SampleConfigPath) {
        $SampleConfigPath = Join-Path $RepoRoot 'bin/config-dist.yaml'
    }
    if (-not (Test-Path -LiteralPath $SampleConfigPath)) {
        throw "Sample config not found at '$SampleConfigPath'."
    }
    $SampleConfigPath = (Resolve-Path -LiteralPath $SampleConfigPath).Path
} elseif (-not (Test-Path -LiteralPath $MsiPath)) {
    throw "MSI not found at '$MsiPath'."
} else {
    $MsiPath = (Resolve-Path -LiteralPath $MsiPath).Path
}

$repoVersion = Get-RepoVersion -PropertiesPath (Join-Path $RepoRoot 'version.properties')
$versionOverridden = $PSBoundParameters.ContainsKey('Version')
if (-not $Version) { $Version = $repoVersion.Short }
if (-not $ReleaseTag) {
    # An overridden -Version must not keep the checkout's tag: the MSI would not be under it.
    $ReleaseTag = if ($versionOverridden) { "v$Version" } else { "v$($repoVersion.Long)" }
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $RepoRoot 'release/winget' }

$msiName = if ($buildMsi) { "meshtasticd-$Version-$Architecture.msi" } else { Split-Path -Leaf $MsiPath }
if (-not $InstallerUrl) {
    $InstallerUrl = "https://github.com/meshtastic/firmware/releases/download/$ReleaseTag/$msiName"
}

$idParts = $PackageIdentifier.Split('.')
if ($idParts.Count -lt 2) {
    throw "PackageIdentifier '$PackageIdentifier' must be at least 'Publisher.Package'."
}
$manifestSegments = @('manifests', $idParts[0].Substring(0, 1).ToLowerInvariant()) + $idParts + @($Version)
$manifestDir = Join-Path $OutputDirectory ($manifestSegments -join [IO.Path]::DirectorySeparatorChar)

$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$null = New-Item -ItemType Directory -Path $manifestDir -Force

# Derived, not read back from the MSI, so -MsiPath produces the same value the build did.
$productCode = (New-DeterministicGuid -Namespace $UpgradeCode -Name "$Version-$Architecture").ToString('B').ToUpperInvariant()

if ($buildMsi) {
    $msiPath = Join-Path $OutputDirectory $msiName
    $wix = Get-WixCommand
    & $wix build $WxsPath `
        -arch $Architecture `
        -ext WixToolset.Util.wixext `
        -d Version=$Version `
        -d ProductCode=$productCode `
        -d BinaryPath=$BinaryPath `
        -d ConfigPath=$SampleConfigPath `
        -o $msiPath
    if ($LASTEXITCODE -ne 0) {
        throw "wix build failed with exit code $LASTEXITCODE. If the Util extension is missing: wix extension add -g WixToolset.Util.wixext/5.0.2"
    }
} else {
    $msiPath = $MsiPath
}

$sha256 = (Get-FileHash -LiteralPath $msiPath -Algorithm SHA256).Hash
$releaseDate = (Get-Date).ToString('yyyy-MM-dd')

$versionManifest = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.$ManifestVersion.schema.json

PackageIdentifier: $PackageIdentifier
PackageVersion: $Version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: $ManifestVersion
"@

$installerManifest = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.$ManifestVersion.schema.json

PackageIdentifier: $PackageIdentifier
PackageVersion: $Version
MinimumOSVersion: 10.0.17763.0
InstallerType: msi
Scope: machine
InstallModes:
  - interactive
  - silent
  - silentWithProgress
UpgradeBehavior: install
ReleaseDate: $releaseDate
Installers:
  - Architecture: $Architecture
    InstallerUrl: $InstallerUrl
    InstallerSha256: $sha256
    ProductCode: '$productCode'
ManifestType: installer
ManifestVersion: $ManifestVersion
"@

$localeManifest = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.defaultLocale.$ManifestVersion.schema.json

PackageIdentifier: $PackageIdentifier
PackageVersion: $Version
PackageLocale: en-US
Publisher: Meshtastic
PublisherUrl: https://meshtastic.org
PublisherSupportUrl: https://github.com/meshtastic/firmware/issues
PackageName: meshtasticd
PackageUrl: https://github.com/meshtastic/firmware
License: GPL-3.0-only
LicenseUrl: https://github.com/meshtastic/firmware/blob/master/LICENSE
ShortDescription: Meshtastic daemon for attached LoRa radio hardware
Description: |-
  meshtasticd is the native build of the Meshtastic firmware. It drives a LoRa radio attached
  over SPI or a CH341 USB bridge and exposes the same TCP API as a Meshtastic device, so any
  Meshtastic client can connect to it. Installs as an auto-starting Windows service reading
  %ProgramData%\Meshtastic\config.yaml; without a radio configured there it runs in
  simulated-radio mode.
Moniker: meshtasticd
Tags:
  - daemon
  - lora
  - mesh
  - meshtastic
  - radio
ReleaseNotesUrl: https://github.com/meshtastic/firmware/releases/tag/$ReleaseTag
Documentations:
  - DocumentLabel: Linux Native Docs
    DocumentUrl: https://meshtastic.org/docs/software/linux/
ManifestType: defaultLocale
ManifestVersion: $ManifestVersion
"@

Set-Content -LiteralPath (Join-Path $manifestDir "$PackageIdentifier.yaml") -Value $versionManifest -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $manifestDir "$PackageIdentifier.installer.yaml") -Value $installerManifest -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $manifestDir "$PackageIdentifier.locale.en-US.yaml") -Value $localeManifest -Encoding utf8NoBOM

Write-Host "Installer    $msiPath"
Write-Host "SHA256       $sha256"
Write-Host "ProductCode  $productCode"
Write-Host "Manifests    $manifestDir"
Write-Host "InstallerUrl $InstallerUrl"

if ($Validate) {
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) {
        Write-Warning 'winget not found on PATH; skipping manifest validation.'
    } else {
        winget validate --manifest $manifestDir
        if ($LASTEXITCODE -ne 0) {
            throw "winget validate failed with exit code $LASTEXITCODE."
        }
    }
}

Write-Host ''
Write-Host "Install locally (elevated):  msiexec /i `"$msiPath`" /qn"
Write-Host "Then check:                  sc query meshtasticd"

# The banner above goes to the host; callers get the same values on the pipeline.
[pscustomobject]@{
    Version           = $Version
    ReleaseTag        = $ReleaseTag
    Architecture      = $Architecture
    MsiPath           = $msiPath
    Sha256            = $sha256
    ProductCode       = $productCode
    InstallerUrl      = $InstallerUrl
    ManifestDirectory = $manifestDir
}
