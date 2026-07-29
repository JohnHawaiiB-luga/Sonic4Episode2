<#
.SYNOPSIS
    Launches an act and records the game window while you play it.

.DESCRIPTION
    Captures the viewer's own window with ffmpeg's gdigrab, so the clip holds the
    game and nothing else on the desktop.

    Two things here are not guesses, deliberately. Readiness is taken from the
    game's own log line rather than a fixed sleep, because an act is several
    million triangles and how long it takes to assemble depends on the machine.
    And the window title is read live, because it carries the ring count and so
    changes as you play - gdigrab matches a title exactly, and the obvious
    hardcoded "Sonic 4 Episode II" never matches anything.

    Raw clips land in analysis/capture/, which is gitignored along with the rest
    of the game-derived output. Copy the ones worth keeping into docs/images/;
    that directory is the curated, committed half.

.EXAMPLE
    .\tools\record.ps1 -Act G_ZONE1/MAP/ZONE11_MAP.AMB

.EXAMPLE
    .\tools\record.ps1 -Act G_ZONE1/MAP/ZONE11_MAP.AMB,G_ZONEF/MAP/ZONEF1_MAP.AMB -Seconds 10
#>
[CmdletBinding()]
param(
    [string[]] $Act = @('G_ZONE1/MAP/ZONE11_MAP.AMB'),
    [int]      $Seconds = 10,
    [string]   $GameRoot = 'C:\Users\DavidErikGarciaArena\Downloads\Sonic 4 - Episode 2 (Beta 8)\Sonic 4 - Episode 2 (Beta 8)',
    [int]      $ReadyTimeout = 120,
    [int]      $Fps = 30,
    [string]   $OutDir = 'analysis/capture'
)

$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot '..\src\Sonic4Episode2.Desktop'
$processName = 'Sonic4Episode2.Desktop'
$readyMarker = 'stage geometry uploaded'

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { throw 'ffmpeg is not on PATH.' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Add-Type -Namespace Win -Name Api -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
'@

foreach ($a in $Act) {
    $name = ($a -replace '.*/', '') -replace '\.AMB$', ''
    $out = Join-Path $OutDir "$name.mp4"
    $log = Join-Path ([IO.Path]::GetTempPath()) "s4e2-$name.log"
    Remove-Item $log -ErrorAction SilentlyContinue
    Write-Host "`n=== $a ===" -ForegroundColor Cyan

    # Every path here contains spaces, and Start-Process joins an argument array
    # without quoting any of it, so the quotes have to be written in.
    $arguments = 'run --project "{0}" --no-build -- "{1}" "{2}"' -f $project, $GameRoot, $a
    $game = Start-Process -FilePath 'dotnet' -PassThru -RedirectStandardOutput $log `
                          -ArgumentList $arguments

    try {
        # Wait for the act to actually finish mounting, not for a guessed delay.
        Write-Host 'loading' -NoNewline
        $deadline = (Get-Date).AddSeconds($ReadyTimeout)
        $ready = $false
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 500
            Write-Host '.' -NoNewline
            if ((Test-Path $log) -and (Select-String -Path $log -Pattern $readyMarker -Quiet -ErrorAction SilentlyContinue)) {
                $ready = $true; break
            }
            if ($game.HasExited) { break }
        }
        if (-not $ready) { Write-Warning "act never reported '$readyMarker'"; continue }
        Write-Host ' ready.'

        # The title carries the ring count, so read it rather than assume it.
        $window = $null
        foreach ($i in 1..20) {
            $window = Get-Process -Name $processName -ErrorAction SilentlyContinue |
                      Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
            if ($window) { break }
            Start-Sleep -Milliseconds 250
        }
        if (-not $window) { Write-Warning 'no game window found'; continue }

        [Win.Api]::SetForegroundWindow($window.MainWindowHandle) | Out-Null
        $title = $window.MainWindowTitle
        Write-Host "capturing window: $title"

        foreach ($n in 3..1) { Write-Host "$n..." -NoNewline; Start-Sleep -Seconds 1 }
        Write-Host 'PLAY.' -ForegroundColor Green

        & ffmpeg -hide_banner -loglevel error -y `
            -f gdigrab -draw_mouse 0 -framerate $Fps -i "title=$title" `
            -t $Seconds -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p $out

        if (Test-Path $out) {
            $kb = [int]((Get-Item $out).Length / 1KB)
            Write-Host "wrote $out ($kb KB)" -ForegroundColor Green
        }
    }
    finally {
        if (-not $game.HasExited) { Stop-Process -Id $game.Id -Force -ErrorAction SilentlyContinue }
        Get-Process -Name $processName -ErrorAction SilentlyContinue | Stop-Process -Force
        Start-Sleep -Seconds 1
    }
}

Write-Host "`nClips are in $OutDir. Copy keepers into docs/images/ to commit them."
