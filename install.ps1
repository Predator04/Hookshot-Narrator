<#
  Hookshot-Narrator installer: adds read-aloud to a Hookshot source folder.

  Usage (PowerShell, from this folder):
      .\install.ps1 -Hookshot "C:\path\to\Hookshot"

  The Hookshot folder must be a git checkout of Hookshot v2.1.0 (see README.md). Nothing is downloaded and no game
  files are touched: this only applies hookshot-narrator.patch to the source. Then build Hookshot as usual.
  To remove it again:  .\install.ps1 -Hookshot "C:\path\to\Hookshot" -Uninstall
#>
param(
    [Parameter(Mandatory = $true)][string]$Hookshot,
    [switch]$Uninstall
)
$patch = Join-Path $PSScriptRoot 'hookshot-narrator.patch'

# (runs git quietly; true when it succeeded. Windows PowerShell treats git's messages as errors, so they are swallowed here)
function Invoke-Git([string[]]$GitArgs, [switch]$Quiet) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    if ($Quiet) { & git @GitArgs 2>&1 | Out-Null } else { & git @GitArgs 2>&1 | ForEach-Object { Write-Host "  $_" } }
    $ok = $LASTEXITCODE -eq 0
    $ErrorActionPreference = $old
    return $ok
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host 'Git is needed: install Git for Windows (https://git-scm.com) and run this again.' -ForegroundColor Red; exit 1
}
if (-not (Test-Path (Join-Path $Hookshot 'nx2\make_game.py')) -or -not (Test-Path (Join-Path $Hookshot 'host\nn_fs.c'))) {
    Write-Host "$Hookshot does not look like a Hookshot source folder (no nx2\make_game.py or host\nn_fs.c)." -ForegroundColor Red; exit 1
}

Push-Location $Hookshot
try {
    $installed = Invoke-Git @('apply', '--reverse', '--check', '--whitespace=nowarn', $patch) -Quiet
    if ($Uninstall) {
        if (-not $installed) {
            Write-Host 'Hookshot-Narrator is not installed in this folder (or its files were changed since).' -ForegroundColor Red; exit 1
        }
        if (Invoke-Git @('apply', '--reverse', '--whitespace=nowarn', $patch)) {
            Write-Host 'Hookshot-Narrator removed. Build Hookshot again to take it out of the game.' -ForegroundColor Green
        } else { exit 1 }
        return
    }
    if ($installed) { Write-Host 'Hookshot-Narrator is already installed in this folder.' -ForegroundColor Green; return }
    if (-not (Invoke-Git @('apply', '--check', '--whitespace=nowarn', $patch) -Quiet)) {
        Write-Host 'The patch does not fit this Hookshot version. Hookshot-Narrator is made for Hookshot v2.1.0: in the Hookshot folder run "git checkout v2.1.0", or see README.md.' -ForegroundColor Red
        exit 1
    }
    if (Invoke-Git @('apply', '--whitespace=nowarn', $patch)) {
        Write-Host 'Hookshot-Narrator installed. Next: build Hookshot as usual (README.md, step 3), then add the voices (step 4).' -ForegroundColor Green
    } else { exit 1 }
} finally {
    Pop-Location
}
