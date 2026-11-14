<#
.SYNOPSIS
Longshot-Narrator installer: adds read-aloud dialogue to a Longshot source folder.

.DESCRIPTION
Applies longshot-narrator.patch to a git checkout of Longshot. Nothing is downloaded and no game files are
touched — this only edits source code. Build Longshot afterwards for the change to reach the game.

    .\install.ps1 -Longshot "C:\path\to\Longshot"
    .\install.ps1 -Longshot "C:\path\to\Longshot" -Uninstall

The folder must be a git checkout of the Longshot version this release supports (see README.md). Running it
twice is harmless: it notices when the add-on is already in place.
#>
param(
    [Parameter(Mandatory = $true)][Alias('Hookshot', 'Source')][string]$Longshot,
    [switch]$Uninstall
)

$TARGET = 'v3.0.0-rc.3'   # the Longshot version this release's patch is cut against
$patch = Join-Path $PSScriptRoot 'longshot-narrator.patch'

# (runs git quietly; true when it succeeded. Windows PowerShell treats git's messages as errors, so they are swallowed here)
function Invoke-Git([string[]]$GitArgs, [switch]$Quiet) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    if ($Quiet) { & git @GitArgs 2>&1 | Out-Null } else { & git @GitArgs 2>&1 | ForEach-Object { Write-Host "  $_" } }
    $ok = $LASTEXITCODE -eq 0
    $ErrorActionPreference = $old
    return $ok
}

if (-not (Test-Path $patch)) {
    Write-Host "longshot-narrator.patch is not beside this script. Unzip the whole release together." -ForegroundColor Red; exit 1
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host 'Git is needed: install Git for Windows (https://git-scm.com) and run this again.' -ForegroundColor Red; exit 1
}
if (-not (Test-Path (Join-Path $Longshot 'nx2\make_game.py')) -or -not (Test-Path (Join-Path $Longshot 'host\nn_fs.c'))) {
    Write-Host "$Longshot does not look like a Longshot source folder (no nx2\make_game.py or host\nn_fs.c)." -ForegroundColor Red
    Write-Host 'Point -Longshot at the source you cloned, not at the built game folder.' -ForegroundColor Red
    exit 1
}

Push-Location $Longshot
try {
    $installed = Invoke-Git @('apply', '--reverse', '--check', '--whitespace=nowarn', $patch) -Quiet

    if ($Uninstall) {
        if (-not $installed) {
            Write-Host 'Longshot-Narrator is not installed in this folder (or its files were changed since).' -ForegroundColor Red; exit 1
        }
        if (Invoke-Git @('apply', '--reverse', '--whitespace=nowarn', $patch)) {
            Write-Host 'Longshot-Narrator removed. Build Longshot again to take it out of the game.' -ForegroundColor Green
        } else { exit 1 }
        return
    }

    if ($installed) { Write-Host 'Longshot-Narrator is already installed in this folder.' -ForegroundColor Green; return }

    if (-not (Invoke-Git @('apply', '--check', '--whitespace=nowarn', $patch) -Quiet)) {
        # say what they are actually on, which is usually the whole answer.
        # (prefer a v... tag: Longshot also carries moving tags called "rc" and "nightly", and git describe
        #  picks those, which tells the reader nothing)
        $ErrorActionPreference = 'Continue'
        $tags = @(& git tag --points-at HEAD 2>$null)
        $at = $tags | Where-Object { $_ -like 'v*' } | Select-Object -First 1
        if (-not $at) { $at = $tags | Select-Object -First 1 }
        if (-not $at) { $at = (& git rev-parse --short HEAD 2>$null) }
        $ErrorActionPreference = 'Stop'
        Write-Host ''
        Write-Host "The patch does not fit the source in $Longshot." -ForegroundColor Red
        if ($at) { Write-Host "  that folder is at: $at" -ForegroundColor Red }
        Write-Host "  this release is for: $TARGET" -ForegroundColor Red
        Write-Host ''
        Write-Host "In the Longshot folder run:  git checkout $TARGET" -ForegroundColor Yellow
        Write-Host 'Or take the Longshot-Narrator release that matches your version. See README.md.' -ForegroundColor Yellow
        exit 1
    }

    if (Invoke-Git @('apply', '--whitespace=nowarn', $patch)) {
        Write-Host 'Longshot-Narrator installed.' -ForegroundColor Green
        Write-Host '  next: build Longshot as usual (README.md, step 3), then run setup-voices.ps1 (step 4).' -ForegroundColor DarkGray
    } else { exit 1 }
} finally {
    Pop-Location
}
