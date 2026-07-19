# CTR-AP support bundle (Windows). Run via support-bundle.bat.
#
# Packs the logs + state needed for a bug report into one zip you can drag
# into Discord. No game data and no passwords are included: the config copies
# have their password lines stripped.
$ErrorActionPreference = "SilentlyContinue"
Set-Location -Path $PSScriptRoot

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$tmp = Join-Path $env:TEMP "ctr-ap-support-$stamp"
New-Item -ItemType Directory -Path $tmp | Out-Null

# Logs + state ("Crash Team Racing*.log" = the engine log).
foreach ($f in @("ctr-ap.log", "ctr-ap.log.old", "ctr-ap-crash.txt", "ap-state.json")) {
    if (Test-Path $f) { Copy-Item $f $tmp }
}
Get-ChildItem "Crash Team Racing*.log" | Copy-Item -Destination $tmp

# Configs, passwords stripped.
foreach ($c in @("config.ini", "ap-config.txt")) {
    if (Test-Path $c) {
        Get-Content $c | Where-Object { $_ -notmatch "^\s*password" } |
            Set-Content (Join-Path $tmp $c)
    }
}

@(
    "CTR-AP support bundle $stamp"
    [Environment]::OSVersion.VersionString
    if (Test-Path "ctr_native_ap.exe") { (Get-FileHash -Algorithm MD5 "ctr_native_ap.exe").Hash }
) | Set-Content (Join-Path $tmp "bundle-info.txt")

$out = "ctr-ap-support-$stamp.zip"
Compress-Archive -Path (Join-Path $tmp "*") -DestinationPath $out -Force
Remove-Item -Recurse -Force $tmp

Write-Host ""
Write-Host "Created: $out"
Write-Host "Attach that file to your bug report (Discord or GitHub). Thanks!"
