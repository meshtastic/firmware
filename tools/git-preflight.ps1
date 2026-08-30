$ErrorActionPreference = "Continue"

Write-Host "Codex Meshtastic preflight"
Write-Host "Current directory: $(Get-Location)"

$root = git rev-parse --show-toplevel 2>$null
if ($LASTEXITCODE -ne 0 -or -not $root) {
    Write-Warning "Not inside a git repository."
    exit 1
}

Write-Host "Repository root: $root"

$branch = git branch --show-current
$head = git rev-parse HEAD

Write-Host "Current branch: $branch"
Write-Host "HEAD: $head"

Write-Host ""
Write-Host "Git status --short:"
$status = git status --short
if ($status) {
    $status
    Write-Warning "Working tree is not clean. Review this before modifying code."
} else {
    Write-Host "clean"
}

Write-Host ""
Write-Host "Remotes:"
git remote -v

Write-Host ""
Write-Host "Firmware target reminder: heltec-v4-r8-tft"
Write-Host "Known-good flash mode: --flash-mode dio"

Write-Host ""
Write-Host "Device UI dependency reference:"
if (Test-Path "platformio.ini") {
    Select-String -Path "platformio.ini" -Pattern "\[device-ui_base\]|device-ui/archive|meshtastic/device-ui" -Context 0,2
} else {
    Write-Warning "platformio.ini not found in current directory."
}

if ($branch -match "^(baseline/.*|main|master|develop)$") {
    Write-Warning "Current branch looks like a baseline or shared branch. Do not develop directly here."
}

Write-Host ""
Write-Host "Do not modify code until preflight is reviewed."
