param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'
$pilotDirectory = Split-Path -Parent $Executable
$pilotName = Split-Path -Leaf $Executable

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

$valid = Invoke-Pilot -Lines @('0 3', '1 1', '4294967295 1')
$expectedOutput = "1013904223 15470`n904494618 13801`n3120439585 47614`n1015567748 15496`n1012240698 15445`n"
$normalizedOutput = $valid.Output -replace "`r`n", "`n"
if ($valid.ExitCode -ne 0 -or $normalizedOutput -ne $expectedOutput -or $valid.ErrorOutput) {
    throw "Valid protocol request failed: exit=$($valid.ExitCode), output=[$($valid.Output)], stderr=[$($valid.ErrorOutput)]"
}

$zeroStep = Invoke-Pilot -Lines @('0 0')
if ($zeroStep.ExitCode -ne 0 -or $zeroStep.Output -ne '' -or $zeroStep.ErrorOutput) {
    throw "Zero-step request failed: exit=$($zeroStep.ExitCode), output=[$($zeroStep.Output)], stderr=[$($zeroStep.ErrorOutput)]"
}

foreach ($invalidInput in @('', '-1 1', '4294967296 1', '0 1000001', '0 1 extra', '18446744073709551616 1')) {
    $invalid = Invoke-Pilot -Lines @($invalidInput)
    if ($invalid.ExitCode -eq 0 -or $invalid.ErrorOutput -notmatch 'invalid request') {
        throw "Malformed request was accepted: input=[$invalidInput], exit=$($invalid.ExitCode), stderr=[$($invalid.ErrorOutput)]"
    }
}
