<#
.SYNOPSIS
    Launches an act and records the game window while you play it.

.DESCRIPTION
    Captures the viewer's own window with ffmpeg's gdigrab, so the clip holds the
    game and nothing else on the desktop.

    Two things here are not guesses, deliberately. Readiness is taken from the
    game's own log line rather than a fixed sleep, because an act is several
    million triangles and how long it takes to assemble depends on the machine.
    And the capture is aimed at the window's measured client rectangle rather
    than at its title: the title carries the live ring count, so it changes the
    moment you land, and gdigrab's title match then fails mid-run.

    Because a screen rectangle records whatever is on top of it, the game is
    pushed to the front and that is confirmed against GetForegroundWindow before
    ffmpeg starts - an unconfirmed raise silently records the window in front.

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

# PowerShell 7.4 makes a native command's non-zero exit honour ErrorActionPreference,
# so one failed ffmpeg would abort the whole run and take the remaining acts with
# it. A failed act should cost that act only. (No such variable on Windows
# PowerShell 5.1, where setting it is harmless.)
$PSNativeCommandUseErrorActionPreference = $false

$project = Join-Path $PSScriptRoot '..\src\Sonic4Episode2.Desktop'
$processName = 'Sonic4Episode2.Desktop'
$readyMarker = 'stage geometry uploaded'

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { throw 'ffmpeg is not on PATH.' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Split on commas ourselves. `powershell -File script.ps1 -Act a,b,c` hands the
# whole list over as ONE string, because -File passes arguments literally rather
# than parsing them as PowerShell, so a [string[]] parameter still receives a
# single element. Splitting here means the same command works whether it was
# invoked with -File, with -Command, or from inside a PowerShell session.
$Act = $Act | ForEach-Object { $_ -split ',' } |
       ForEach-Object { $_.Trim() } | Where-Object { $_ }

# Fail on a bad act path before launching, rather than waiting out the timeout.
foreach ($a in $Act) {
    if (-not (Test-Path (Join-Path $GameRoot ($a -replace '/', '\')))) {
        throw "act archive not found under the game root: $a"
    }
}
Write-Host ("recording {0} act(s), {1}s each" -f $Act.Count, $Seconds)

# Capture by screen rectangle, not by window title. The title carries the live
# ring count and rolling state, so it changes the moment the player lands - which
# made ffmpeg fail with "Can't find window" on whichever acts happened to change
# state during the countdown. The client rect is stable and also excludes the
# title bar, so the clip is pure game.
#
# Guarded, because Add-Type throws on a type that already exists: without this the
# script runs once per PowerShell session and every re-run after that fails.
if (-not ('WinApi' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public struct RECT { public int Left, Top, Right, Bottom; }
public static class WinApi {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref System.Drawing.Point p);
}
'@ -ReferencedAssemblies System.Drawing, System.Drawing.Primitives
}

# This script has to be DPI-aware too, or GetClientRect reports virtualised
# coordinates: on a 125% display a 1280x720 game window measured 1024x576 and the
# capture came out that size, downscaled.
[WinApi]::SetProcessDPIAware() | Out-Null

$actNo = 0
foreach ($a in $Act) {
    $actNo++
    $name = ($a -replace '.*/', '') -replace '\.AMB$', ''
    $out = Join-Path $OutDir "$name.mp4"
    $log = Join-Path ([IO.Path]::GetTempPath()) "s4e2-$name.log"
    Remove-Item $log -ErrorAction SilentlyContinue
    # Numbered, so a list that arrived as one unsplit string is obvious on sight
    # rather than looking like a single act with a very long name.
    Write-Host ("`n=== [{0}/{1}] {2} ===" -f $actNo, $Act.Count, $a) -ForegroundColor Cyan

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
        if (-not $ready) {
            # Say why, rather than only that. The game's own output holds the
            # reason and reading it back beats guessing at a silent failure.
            Write-Warning "act never reported '$readyMarker'"
            if (Test-Path $log) {
                Write-Host '--- game output ---' -ForegroundColor DarkYellow
                Get-Content $log -Tail 15 | ForEach-Object { Write-Host "  $_" }
                Write-Host '-------------------' -ForegroundColor DarkYellow
            } else {
                Write-Host '  (the game produced no output at all)'
            }
            continue
        }
        Write-Host ' ready.'

        # The window appears a moment after the act reports ready, so poll for it.
        $window = $null
        foreach ($i in 1..20) {
            $window = Get-Process -Name $processName -ErrorAction SilentlyContinue |
                      Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
            if ($window) { break }
            Start-Sleep -Milliseconds 250
        }
        if (-not $window) { Write-Warning 'no game window found'; continue }

        # Raise it properly. Capturing a screen rectangle means anything painting
        # over the game is recorded instead of it, and SetForegroundWindow on its
        # own reports success while another window still covers the game - the
        # same trap analysis/shot.ps1 documents. Restore, pin topmost, activate,
        # then confirm against GetForegroundWindow rather than trusting any of it.
        $handle = $window.MainWindowHandle
        [WinApi]::ShowWindow($handle, 9) | Out-Null                                  # SW_RESTORE
        [WinApi]::SetWindowPos($handle, [IntPtr]::new(-1), 0, 0, 0, 0, 0x0003) | Out-Null  # HWND_TOPMOST, NOMOVE|NOSIZE
        [WinApi]::SetForegroundWindow($handle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([WinApi]::GetForegroundWindow() -ne $handle) {
            Write-Warning 'the game window would not come to the front - another window would be recorded instead'
            continue
        }

        $rect = New-Object RECT
        if (-not [WinApi]::GetClientRect($handle, [ref] $rect)) {
            Write-Warning 'could not measure the game window'; continue
        }
        $origin = New-Object System.Drawing.Point 0, 0
        [WinApi]::ClientToScreen($handle, [ref] $origin) | Out-Null

        # h.264 needs even dimensions.
        $w = ($rect.Right - $rect.Left) -band -2
        $h = ($rect.Bottom - $rect.Top) -band -2
        if ($w -le 0 -or $h -le 0) { Write-Warning 'game window has no client area'; continue }
        Write-Host ("capturing {0}x{1} at {2},{3}" -f $w, $h, $origin.X, $origin.Y)

        foreach ($n in 3..1) { Write-Host "$n..." -NoNewline; Start-Sleep -Seconds 1 }
        Write-Host 'PLAY.' -ForegroundColor Green

        & ffmpeg -hide_banner -loglevel error -y `
            -f gdigrab -draw_mouse 0 -framerate $Fps `
            -offset_x $origin.X -offset_y $origin.Y -video_size "${w}x${h}" -i desktop `
            -t $Seconds -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p $out

        if ($LASTEXITCODE -ne 0) {
            Write-Warning "ffmpeg failed for $name (exit $LASTEXITCODE) - no clip written"
        }
        elseif (Test-Path $out) {
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
