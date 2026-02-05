<#
    .SYNOPSIS
    Unit-test for .\device-install.bat.

    .DESCRIPTION
    This script performs a positive unit-test on .\device-install.bat by creating the expected .bin
    files for a device followed by running the .bat script without flashing the firmware (--debug).
    If any errors are hit they are presented in the standard output. Investigate accordingly.

    This script needs to be placed in the same directory as .\device-install.bat.

    .EXAMPLE
    .\device-install_test.ps1

    .EXAMPLE
    .\device-install_test.ps1 -Verbose

    .LINK
    .\device-install.bat --help
#>

[CmdletBinding()]
param()

function New-EmptyFile() {
    [CmdletBinding()]
    param (
        [Parameter(Position = 0, Mandatory = $true)]
        # Specifies the file name.
        [string]$FileName,
        [Parameter(Position = 1)]
        # Specifies the target path. (Get-Location).Path is the default.
        [string]$Directory = (Get-Location).Path
    )

    $filePath = Join-Path -Path $Directory -ChildPath $FileName

    Write-Verbose -Message "Create empty test file if it doesn't exist: $($FileName)"
    New-Item -Path "$filePath" -ItemType File -ErrorAction SilentlyContinue | Out-Null
}

function Remove-EmptyFile() {
    [CmdletBinding()]
    param (
        [Parameter(Position = 0, Mandatory = $true)]
        # Specifies the file name.
        [string]$FileName,
        [Parameter(Position = 1)]
        # Specifies the target path. (Get-Location).Path is the default.
        [string]$Directory = (Get-Location).Path
    )

    $filePath = Join-Path -Path $Directory -ChildPath $FileName

    Write-Verbose -Message "Deleted empty test file: $($FileName)"
    Remove-Item -Path "$filePath" | Out-Null
}


$TestCases = New-Object -TypeName PSObject -Property @{
    # Use this PSObject to define testcases according to this syntax:
    # "testname" = @("firmware-testname","bleota","littlefs-testname","args")
    "t-deck"                       = @("firmware-t-deck-2.6.0.0b106d4.bin", "bleota-s3.bin", "littlefs-t-deck-2.6.0.0b106d4.bin", "")
    "t-deck_web"                   = @("firmware-t-deck-2.6.0.0b106d4.bin", "bleota-s3.bin", "littlefswebui-t-deck-2.6.0.0b106d4.bin", "--web")
    "t-deck-tft"                   = @("firmware-t-deck-tft-2.6.0.0b106d4.bin", "bleota-s3.bin", "littlefs-t-deck-tft-2.6.0.0b106d4.bin", "")
    "heltec-ht62-esp32c3"          = @("firmware-heltec-ht62-esp32c3-sx1262-2.6.0.0b106d4.bin", "bleota-c3.bin", "littlefs-heltec-ht62-esp32c3-sx1262-2.6.0.0b106d4.bin", "")
    "tlora-c6"                     = @("firmware-tlora-c6-2.6.0.0b106d4.bin", "bleota.bin", "littlefs-tlora-c6-2.6.0.0b106d4.bin", "")
    "heltec-v3_web"                = @("firmware-heltec-v3-2.6.0.0b106d4.bin", "bleota-s3.bin", "littlefswebui-heltec-v3-2.6.0.0b106d4.bin", "--web")
    "seeed-sensecap-indicator-tft" = @("firmware-seeed-sensecap-indicator-tft-2.6.0.0b106d4.bin", "bleota.bin", "littlefs-seeed-sensecap-indicator-tft-2.6.0.0b106d4.bin", "")
    "picomputer-s3-tft"            = @("firmware-picomputer-s3-tft-2.6.0.0b106d4.bin", "bleota-s3.bin", "littlefs-picomputer-s3-tft-2.6.0.0b106d4.bin", "")
}

foreach ($TestCase in $TestCases.PSObject.Properties) {
    $Name = $TestCase.Name
    $Files = $TestCase.Value
    $Errors = $null
    $Counter = 0

    Write-Host -Object "Testcase: $Name`:" -ForegroundColor Green
    foreach ($File in $Files) {
        if ($File.EndsWith(".bin")) {
            New-EmptyFile -FileName $File
        }
    }

    Write-Host -Object "Performing test on $Name..." -ForegroundColor Blue
    $Test = Invoke-Expression -Command "cmd /c .\device-install.bat --debug -f $($TestCases."$Name"[0]) $($TestCases."$Name"[3])"

    foreach ($Line in $Test) {
        if ($Line -match "Set OTA_OFFSET to" -or `
                $Line -match "Set SPIFFS_OFFSET to") {
            Write-Host -Object "$($Line -replace "^.*?Set","Set")" -ForegroundColor Blue
        }
        elseif ($VerbosePreference -eq "Continue") {
            Write-Host -Object $Line
        }
        if ($Line -match "ERROR") {
            $Errors += $Line
            $Counter++
        }
    }
    if ($null -ne $Errors) {
        Write-Host -Object "$Counter ERROR(s) detected!" -ForegroundColor Red
        if (-not ($VerbosePreference -eq "Continue")) { Write-Host -Object $Errors }
    }

    foreach ($File in $Files) {
        if ($File.EndsWith(".bin")) {
            Remove-EmptyFile -FileName $File
        }
    }
}
