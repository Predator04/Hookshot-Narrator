<#
.SYNOPSIS
Longshot-Narrator: fetches Piper and the two natural voices into your built game folder.

.DESCRIPTION
Step 4 of the README, done for you. It downloads Piper (the speech engine that runs on your PC) and the
two voice models, and puts them in the "piper" folder beside longshot.exe, which is where the add-on
looks. About 157 MB in all.

Run it AFTER you have built the game, and point it at the folder holding longshot.exe:

    .\setup-voices.ps1 -Game "C:\Games\Longshot"

Already have some of it? Files that are present and the right size are left alone, so it is safe to run
again if a download fails part way.

    .\setup-voices.ps1 -Game "C:\Games\Longshot" -Check     # say what is missing, download nothing

Read-aloud works without any of this by using the Windows voice; Piper is what makes it sound natural.
#>
param(
    [Parameter(Mandatory = $true)][string]$Game,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # (the progress bar makes Invoke-WebRequest far slower)

# (the game folder: longshot.exe; hookshot.exe too, for anyone still on a Hookshot build)
$exe = @('longshot.exe', 'hookshot.exe') | ForEach-Object { Join-Path $Game $_ } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    Write-Host "No longshot.exe or hookshot.exe in $Game." -ForegroundColor Red
    Write-Host 'Point -Game at your BUILT game folder (the one make_game.py wrote with --out), not the source.' -ForegroundColor Red
    exit 1
}
$piper = Join-Path $Game 'piper'

# name -> url, the size we expect, and whether it arrives inside the Piper zip
$voiceBase = 'https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US'
$items = @(
    # (Size is the size of the FILE we check for. piper.exe arrives inside a 21 MB zip but is itself small,
    #  so it is checked for existence only)
    @{ Name = 'piper.exe'; Zip = $true
       Url = 'https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip'
       Size = 0; What = 'Piper, the speech engine (with espeak-ng-data)' }
    @{ Name = 'en_US-lessac-medium.onnx';           Url = "$voiceBase/lessac/medium/en_US-lessac-medium.onnx";           Size = 63201294; What = "the narrator's voice" }
    @{ Name = 'en_US-lessac-medium.onnx.json';      Url = "$voiceBase/lessac/medium/en_US-lessac-medium.onnx.json";      Size = 0;        What = 'its settings' }
    @{ Name = 'en_US-libritts_r-medium.onnx';       Url = "$voiceBase/libritts_r/medium/en_US-libritts_r-medium.onnx";   Size = 78577558; What = "the characters' voices and Link's" }
    @{ Name = 'en_US-libritts_r-medium.onnx.json';  Url = "$voiceBase/libritts_r/medium/en_US-libritts_r-medium.onnx.json"; Size = 0;     What = 'its settings' }
)

function Have($item) {
    $p = Join-Path $piper $item.Name
    if (-not (Test-Path $p)) { return $false }
    # a part-finished download is the usual failure, so check the size where we know it
    if ($item.Size -gt 0 -and (Get-Item $p).Length -lt ($item.Size * 0.95)) { return $false }
    return $true
}

Write-Host ""
Write-Host "Game folder : $Game"
Write-Host "Voices go in: $piper"
Write-Host ""

$missing = @($items | Where-Object { -not (Have $_) })
foreach ($i in $items) {
    $has = Have $i
    $mark = if ($has) { '  have' } else { 'NEEDED' }
    $col = if ($has) { 'DarkGray' } else { 'Yellow' }
    Write-Host ("  [{0}] {1,-34} {2}" -f $mark, $i.Name, $i.What) -ForegroundColor $col
}
Write-Host ""

if ($missing.Count -eq 0) {
    Write-Host 'Everything is already in place. Start the game and talk to anyone.' -ForegroundColor Green
    exit 0
}
if ($Check) {
    Write-Host ("{0} item(s) missing. Run without -Check to fetch them." -f $missing.Count) -ForegroundColor Yellow
    exit 0
}

Write-Host 'These are downloaded from the Piper project and from Hugging Face:' -ForegroundColor Cyan
Write-Host '  Piper   https://github.com/rhasspy/piper          (MIT)' -ForegroundColor Cyan
Write-Host '  Voices  https://huggingface.co/rhasspy/piper-voices' -ForegroundColor Cyan
Write-Host '  Each voice carries its own licence on its model card. Please look before you use it.' -ForegroundColor Cyan
Write-Host ''

New-Item -ItemType Directory -Force -Path $piper | Out-Null

foreach ($i in $missing) {
    if ($i.Zip) {
        $tmp = Join-Path ([System.IO.Path]::GetTempPath()) 'piper_windows_amd64.zip'
        Write-Host ("Downloading {0} (~21 MB)..." -f $i.What)
        Invoke-WebRequest -Uri $i.Url -OutFile $tmp -UseBasicParsing
        Write-Host '  unpacking...'
        # the zip holds a top-level piper\ folder, so this lands as <game>\piper\piper.exe
        Expand-Archive -LiteralPath $tmp -DestinationPath $Game -Force
        Remove-Item $tmp -Force -ErrorAction SilentlyContinue
        if (-not (Test-Path (Join-Path $piper 'piper.exe'))) {
            Write-Host '  piper.exe did not appear where expected; unpack the zip by hand (README step 4).' -ForegroundColor Red
            exit 1
        }
    }
    else {
        $dst = Join-Path $piper $i.Name
        $mb = if ($i.Size -gt 0) { ' ({0:N0} MB)' -f ($i.Size / 1MB) } else { '' }
        Write-Host ("Downloading {0}{1}..." -f $i.Name, $mb)
        Invoke-WebRequest -Uri $i.Url -OutFile $dst -UseBasicParsing
        if ($i.Size -gt 0 -and (Get-Item $dst).Length -lt ($i.Size * 0.95)) {
            Write-Host '  that came back short; run this again to retry.' -ForegroundColor Red
            exit 1
        }
    }
}

Write-Host ''
$left = @($items | Where-Object { -not (Have $_) })
if ($left.Count -eq 0) {
    Write-Host 'Done. Start the game and talk to anyone.' -ForegroundColor Green
    Write-Host 'If you still hear the Windows voice, longshot_log.txt beside the game says why.' -ForegroundColor DarkGray
}
else {
    Write-Host ('Still missing: {0}. Run this again.' -f ($left.Name -join ', ')) -ForegroundColor Red
    exit 1
}
