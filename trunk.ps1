$ErrorActionPreference = 'Stop'

$Env:TRUNK_LAUNCHER_VERSION="1.3.4" # warning: this line is auto-updated
$Env:TRUNK_LAUNCHER_PATH = $PSCommandPath

# Try to get the version from trunk.yaml.
function TryGetTrunkVersion {
    $currentDir = (Get-Location).Path
    while ($currentDir -ne '') {
        $trunkYamlPath = "$currentDir\.trunk\trunk.yaml"
        if (Test-Path "$trunkYamlPath") {
            $yamlContent = Get-Content "$trunkYamlPath" -Raw
            return $yamlContent -replace '(?s).*\s+version:\s*([0-9a-z.-]+).*','$1'
        }
        $currentDir = Split-Path $currentDir -Parent
    }
    return $null
}

# Get the latest version of trunk.
function GetLatestTrunkVersion {
    $latestReleaseInfo = Invoke-RestMethod 'https://trunk.io/releases/latest' -UseBasicParsing
    return $latestReleaseInfo -replace '(?s).*version:\s*([0-9a-z.-]+).*','$1'
}

# Download a particular version of trunk to the specified directory.
function DownloadTrunk($version, $trunkDir) {
    $null = New-Item -Type Directory -Force -Path (Split-Path $trunkDir -Parent)
    $guid = [System.Guid]::NewGuid()
    $downloadPath = "$trunkDir.$guid.zip"
    $destinationPath = "$trunkDir.$guid"
    try {
        # Download the zip file.
        $zipFile = "trunk-$version.windows.zip"
        $zipFilePath = "$tempDir\$zipFile"
        $downloadUrl = "https://trunk.io/releases/$version/$zipFile"
        Invoke-RestMethod -Uri $downloadUrl -OutFile $downloadPath
        # Extract the zip file to a uniquely named directory.
        Expand-Archive -Path $downloadPath -DestinationPath $destinationPath
        # Remove the trunk directory if it already exists.
        if (Test-Path $trunkDir) {
            Remove-Item -Path $trunkDir -Recurse -Force
        }
        # Move the uniquely named trunk directory to the final location.
        Move-Item -Path $destinationPath -Destination $trunkDir
    }
    finally {
        # Cleanup.
        Remove-Item -Path $downloadPath -Force -ea 0
        Remove-Item -Path $destinationPath -Recurse -Force -ea 0
    }
}

# Get the version to run.
$version = TryGetTrunkVersion
if ($version -eq $null) {
    $version = GetLatestTrunkVersion
}

# Determine the expected path to trunk.exe.
$localApplicationData = [Environment]::GetFolderPath('LocalApplicationData')
$trunkDir = "$localApplicationData\trunk\cli\trunk-${version}-windows"
$trunkExe = "$trunkDir\trunk.exe"

# Download trunk.exe if it doesn't exist.
if (!(Test-Path $trunkExe)) {
    DownloadTrunk $version $trunkDir
}

# Execute trunk with all arguments
& $trunkExe $args
exit $LastExitCode
