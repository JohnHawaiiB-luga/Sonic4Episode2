param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'

function Assert-Equal([object]$Actual, [object]$Expected, [string]$Name) {
    if ($Actual -ne $Expected) {
        throw "$Name expected [$Expected], got [$Actual]"
    }
}

function Assert-Array([object[]]$Actual, [object[]]$Expected, [string]$Name) {
    if ($Actual.Count -ne $Expected.Count) {
        throw "$Name length expected [$($Expected.Count)], got [$($Actual.Count)]"
    }
    for ($index = 0; $index -lt $Expected.Count; ++$index) {
        Assert-Equal $Actual[$index] $Expected[$index] "$Name[$index]"
    }
}

$pilotDirectory = Split-Path -Parent $Executable
$pilotName = Split-Path -Leaf $Executable
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Pilot executable not found: $Executable"
}

function Invoke-Pilot([string[]]$Lines) {
    $echoCommands = foreach ($line in $Lines) {
        if ($line.Length -eq 0) {
            'echo('
        }
        else {
            "echo $line"
        }
    }

    $command = '(' + ($echoCommands -join '&') + ') | ".\' + $pilotName + '"'
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $env:ComSpec
    $startInfo.Arguments = "/d /c $command"
    $startInfo.WorkingDirectory = $pilotDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw 'Could not start the pilot executable.'
    }

    $output = $process.StandardOutput.ReadToEnd()
    $errorOutput = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    return [PSCustomObject]@{
        ExitCode = $process.ExitCode
        Output = $output
        ErrorOutput = $errorOutput
    }
}

$emptyRequest = '0 1 0 0 0 1065353216 3221225472 1077936128 1048576000 3204448256 1234 5678'
$nonemptyRequest = '1 1 1 0 1 0 2147483648 1 2147483649 8388607 65535 8191'
$valid = Invoke-Pilot -Lines @($emptyRequest, $nonemptyRequest)
if ($valid.ExitCode -ne 0 -or $valid.ErrorOutput) {
    throw "Valid protocol request failed: exit=$($valid.ExitCode), stderr=[$($valid.ErrorOutput)]"
}

$records = @($valid.Output -split "`r?`n" | Where-Object { $_ })
Assert-Equal $records.Count 2 'valid record count'
$empty = $records[0] | ConvertFrom-Json
Assert-Equal $empty.returned_index 0 'empty returned index'
Assert-Equal $empty.seed 1013904223 'empty seed'
Assert-Equal $empty.cursor 1 'empty cursor'
Assert-Equal $empty.head 0 'empty head'
Assert-Equal $empty.tail 0 'empty tail'
Assert-Equal $empty.previous_tail_next -1 'empty previous tail next'
Assert-Array @($empty.ring.position_bits) @(1065353216, 3221225472, 1077936128) 'empty position'
Assert-Array @($empty.ring.velocity_bits) @(1048576000, 3204448256) 'empty velocity'
Assert-Array @($empty.ring.scale_bits) @(1065353216, 1065353216, 1065353216) 'empty scale'
Assert-Equal $empty.ring.timer 270 'empty timer'
Assert-Equal $empty.ring.parameter_a 1234 'empty parameter a'
Assert-Equal $empty.ring.parameter_b 5678 'empty parameter b'
Assert-Equal $empty.ring.auxiliary_a 0 'empty auxiliary a'
Assert-Equal $empty.ring.auxiliary_b 0 'empty auxiliary b'
Assert-Array @($empty.ring.modulation_bits) @(1065353216, 1065353216, 1065353216, 1065353216) 'empty modulation'
Assert-Equal $empty.ring.previous -1 'empty previous'
Assert-Equal $empty.ring.next -1 'empty next'

$nonempty = $records[1] | ConvertFrom-Json
Assert-Equal $nonempty.returned_index 1 'nonempty returned index'
Assert-Equal $nonempty.seed 1015567748 'nonempty seed'
Assert-Equal $nonempty.cursor 2 'nonempty cursor'
Assert-Equal $nonempty.head 0 'nonempty head'
Assert-Equal $nonempty.tail 1 'nonempty tail'
Assert-Equal $nonempty.previous_tail_next 1 'nonempty previous tail next'
Assert-Array @($nonempty.ring.position_bits) @(0, 2147483648, 1) 'nonempty position'
Assert-Array @($nonempty.ring.velocity_bits) @(2147483649, 8388607) 'nonempty velocity'
Assert-Equal $nonempty.ring.timer 264 'nonempty timer'
Assert-Equal $nonempty.ring.parameter_a 65535 'nonempty parameter a'
Assert-Equal $nonempty.ring.parameter_b 8191 'nonempty parameter b'
Assert-Equal $nonempty.ring.previous 0 'nonempty previous'
Assert-Equal $nonempty.ring.next -1 'nonempty next'

foreach ($failure in @(
    @{ Request = '123 1 96 0 96 0 0 0 0 0 0 0'; Cursor = 96; Head = 0; Tail = 95 },
    @{ Request = '123 1 0 1 0 0 0 0 0 0 0 0'; Cursor = 1; Head = -1; Tail = -1 },
    @{ Request = '123 0 1 0 1 0 0 0 0 0 0 0'; Cursor = 1; Head = 0; Tail = 0 }
)) {
    $failureResult = Invoke-Pilot -Lines @($failure.Request)
    if ($failureResult.ExitCode -ne 0 -or $failureResult.ErrorOutput) {
        throw "Failure protocol request failed: exit=$($failureResult.ExitCode), stderr=[$($failureResult.ErrorOutput)]"
    }
    $failureRecord = $failureResult.Output | ConvertFrom-Json
    Assert-Equal $failureRecord.returned_index -1 'failure returned index'
    Assert-Equal $failureRecord.seed 123 'failure seed'
    Assert-Equal $failureRecord.cursor $failure.Cursor 'failure cursor'
    Assert-Equal $failureRecord.head $failure.Head 'failure head'
    Assert-Equal $failureRecord.tail $failure.Tail 'failure tail'
    Assert-Equal $failureRecord.previous_tail_next -1 'failure previous tail next'
    if ($null -ne $failureRecord.ring) {
        throw 'failure returned a ring object'
    }
}

foreach ($invalidRequest in @(
    '',
    '0 1 0 0 0 0 0 0 0 0 0',
    ($emptyRequest + ' 0'),
    '-1 1 0 0 0 0 0 0 0 0 0 0',
    '4294967296 1 0 0 0 0 0 0 0 0 0 0',
    '0 2 0 0 0 0 0 0 0 0 0 0',
    '0 1 98 0 0 0 0 0 0 0 0 0',
    '0 1 0 2 0 0 0 0 0 0 0 0',
    '0 1 0 0 1 0 0 0 0 0 0 0',
    '0 1 0 0 0 0 0 0 0 0 65536 0',
    '0 1 0 0 0 2139095040 0 0 0 0 0 0'
)) {
    $invalid = Invoke-Pilot -Lines @($invalidRequest)
    if ($invalid.ExitCode -ne 2 -or $invalid.ErrorOutput -notmatch 'invalid request') {
        throw "Malformed request was not rejected: input=[$invalidRequest], exit=$($invalid.ExitCode), stderr=[$($invalid.ErrorOutput)]"
    }
}
