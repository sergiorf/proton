# SPDX-License-Identifier: AGPL-3.0-only
# Fetches the pinned ngspice shared library into third_party/ngspice and checks its SHA-256
# (ADR 0002). Only the files Proton uses are kept:
#   third_party/ngspice/bin/ngspice.dll, bin/libomp140.x86_64.dll
#   third_party/ngspice/include/ngspice/sharedspice.h
#   third_party/ngspice/VERSION
# The code models (*.cm, *.osdi) are not kept: table.cm is GPL and must never be shipped.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools/fetch-ngspice.ps1 [-Force]
[CmdletBinding()]
param(
    # Download again even if the pinned version is already in place.
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# The pin. Changing it means a new SHA-256 and an update of ADR 0002.
$Version = '46'
$Archive = "ngspice-${Version}_dll_64.7z"
$Url = "https://downloads.sourceforge.net/project/ngspice/ng-spice-rework/old-releases/$Version/$Archive"
$Sha256 = '5C03F25709D6EA7D1E58A82EBE7C1E6EB5177F266787466BF0C716791BA26E4C'

# Archive path -> path under third_party/ngspice.
$Keep = [ordered]@{
    'Spice64_dll/dll-vs/ngspice.dll'                = 'bin/ngspice.dll'
    'Spice64_dll/dll-vs/libomp140.x86_64.dll'       = 'bin/libomp140.x86_64.dll'
    'Spice64_dll/include/ngspice/sharedspice.h'     = 'include/ngspice/sharedspice.h'
}

$Root = Split-Path -Parent $PSScriptRoot
$Dest = Join-Path $Root 'third_party/ngspice'
$VersionFile = Join-Path $Dest 'VERSION'

function Test-Complete {
    if (-not (Test-Path $VersionFile)) { return $false }
    if ((Get-Content $VersionFile -Raw).Trim() -ne $Version) { return $false }
    foreach ($target in $Keep.Values) {
        if (-not (Test-Path (Join-Path $Dest $target))) { return $false }
    }
    return $true
}

if (-not $Force -and (Test-Complete)) {
    Write-Host "ngspice $Version is already in $Dest"
    exit 0
}

$Work = Join-Path ([IO.Path]::GetTempPath()) ("proton-ngspice-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $Work | Out-Null
try {
    $ArchivePath = Join-Path $Work $Archive
    Write-Host "Downloading $Url"
    # The progress bar slows Invoke-WebRequest down by an order of magnitude in Windows PowerShell.
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $Url -OutFile $ArchivePath -UseBasicParsing -UserAgent 'Wget'

    $actual = (Get-FileHash -Algorithm SHA256 $ArchivePath).Hash
    if ($actual -ne $Sha256) {
        throw "SHA-256 mismatch for ${Archive}: expected $Sha256, got $actual"
    }

    # 7-Zip if installed (CI runners have it), otherwise the bsdtar that ships with Windows.
    $Extracted = Join-Path $Work 'x'
    New-Item -ItemType Directory -Path $Extracted | Out-Null
    $SevenZip = Get-Command 7z -ErrorAction SilentlyContinue
    if ($SevenZip) {
        & $SevenZip.Source x $ArchivePath "-o$Extracted" -y | Out-Null
    } else {
        & (Join-Path $env:SystemRoot 'System32/tar.exe') -xf $ArchivePath -C $Extracted
    }
    if ($LASTEXITCODE -ne 0) { throw "Extracting $Archive failed (exit code $LASTEXITCODE)" }

    # Stage the kept files, then replace third_party/ngspice in one step.
    $Staging = Join-Path $Work 'ngspice'
    foreach ($entry in $Keep.GetEnumerator()) {
        $from = Join-Path $Extracted $entry.Key
        if (-not (Test-Path $from)) { throw "$Archive does not contain $($entry.Key)" }
        $to = Join-Path $Staging $entry.Value
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
        Copy-Item $from $to
    }
    Set-Content -Path (Join-Path $Staging 'VERSION') -Value $Version -Encoding ascii

    if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dest) | Out-Null
    Move-Item $Staging $Dest
    Write-Host "ngspice $Version installed in $Dest"
} finally {
    Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
}
