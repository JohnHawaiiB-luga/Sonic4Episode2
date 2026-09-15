param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'
$stageExecutable = (Resolve-Path -LiteralPath $Executable).Path
$stageDirectory = Split-Path -Parent $stageExecutable
$stageFixtureDirectory = Join-Path $stageDirectory ('stage-cli-test-' + [Guid]::NewGuid().ToString('N'))

function Invoke-StageInspector([string[]]$Arguments) {
    $quoted = foreach ($argument in $Arguments) {
        if ($argument.Contains('"')) { throw 'Unexpected quote in test argument' }
        '"' + $argument + '"'
    }
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $stageExecutable
    $startInfo.Arguments = $quoted -join ' '
    $startInfo.WorkingDirectory = $stageDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'Could not start stage inspector' }
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(10000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'Stage inspector exceeded the test deadline'
        }
        [PSCustomObject]@{
            ExitCode = $process.ExitCode
            Output = $outputTask.GetAwaiter().GetResult()
            ErrorOutput = $errorTask.GetAwaiter().GetResult()
        }
    }
    finally {
        $process.Dispose()
    }
}

function Assert-Stage([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Write-Word([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
    for ($shift = 0; $shift -lt 4; ++$shift) {
        $Buffer[$Offset + $shift] = [byte](($Value -shr ($shift * 8)) -band 255)
    }
}

[void][IO.Directory]::CreateDirectory($stageFixtureDirectory)
$stageUnicodeName = 'synthetic ' + [char]0x00e9 + ' ' + [char]0x65e5 + [char]0x672c + '.AMB'
$stageValidPath = Join-Path $stageFixtureDirectory $stageUnicodeName
$stageInvalidPath = Join-Path $stageFixtureDirectory 'invalid.AMB'
try {
    $data = [byte[]]::new(148)
    [Text.Encoding]::ASCII.GetBytes('#AMB').CopyTo($data, 0)
    Write-Word $data 4 32
    Write-Word $data 8 262144
    Write-Word $data 16 2
    Write-Word $data 20 32
    Write-Word $data 24 64
    Write-Word $data 28 84
    Write-Word $data 32 64
    Write-Word $data 36 12
    Write-Word $data 44 2166572292
    Write-Word $data 48 76
    Write-Word $data 52 8
    Write-Word $data 60 4294967295
    ([byte[]](2, 0, 2, 0, 0x34, 0x12, 0, 0x80, 0xff, 0xff, 1, 0)).CopyTo($data, 64)
    ([byte[]](2, 0, 2, 0, 0, 1, 0x80, 0xff)).CopyTo($data, 76)
    $rawName = 'quote"\path' + [char]10 + [char]255 + '.MP'
    for ($index = 0; $index -lt $rawName.Length; ++$index) {
        $data[84 + $index] = [byte][char]$rawName[$index]
    }
    [Text.Encoding]::ASCII.GetBytes('second.md').CopyTo($data, 116)
    [IO.File]::WriteAllBytes($stageValidPath, $data)

    foreach ($withCells in @($false, $true)) {
        $arguments = @($stageValidPath)
        if ($withCells) { $arguments += '--cells' }
        $result = Invoke-StageInspector $arguments
        Assert-Stage ($result.ExitCode -eq 0 -and -not $result.ErrorOutput) 'Valid archive failed'
        $parsed = $result.Output | ConvertFrom-Json
        Assert-Stage ($parsed.format -eq 'pc-amb32' -and $parsed.entry_count -eq 2 -and $parsed.grid_count -eq 2) 'Archive summary differs'
        Assert-Stage ($parsed.entries[0].name -ceq $rawName) 'Raw archive name was not preserved in JSON'
        Assert-Stage ($parsed.entries[0].offset -eq 64 -and $parsed.entries[0].length -eq 12) 'Payload descriptor differs'
        Assert-Stage ($parsed.entries[0].flags -eq 2166572292 -and $parsed.entries[1].flags -eq 4294967295) 'Unsigned archive flags differ'
        $wordGrid = $parsed.entries[0].grid
        $byteGrid = $parsed.entries[1].grid
        Assert-Stage ($wordGrid.width -eq 2 -and $wordGrid.height -eq 2 -and $wordGrid.cell_bytes -eq 2 -and $wordGrid.cell_count -eq 4 -and $wordGrid.nonzero_cells -eq 4 -and $wordGrid.max_cell -eq 65535) 'Word grid summary differs'
        Assert-Stage ($byteGrid.cell_bytes -eq 1 -and $byteGrid.nonzero_cells -eq 3 -and $byteGrid.max_cell -eq 255) 'Byte grid summary differs'
        if ($withCells) {
            Assert-Stage (($wordGrid.cells -join ',') -eq '4660,32768,65535,1') 'Word grid cells differ'
            Assert-Stage (($byteGrid.cells -join ',') -eq '0,1,128,255') 'Byte grid cells differ'
        }
        else {
            Assert-Stage (-not ($wordGrid.PSObject.Properties.Name -contains 'cells')) 'Summary mode emitted full cells'
        }
    }

    foreach ($length in @(0, 31, 47, 83, 147)) {
        $truncated = [byte[]]::new($length)
        [Array]::Copy($data, $truncated, $length)
        [IO.File]::WriteAllBytes($stageInvalidPath, $truncated)
        $result = Invoke-StageInspector @($stageInvalidPath)
        Assert-Stage ($result.ExitCode -eq 1 -and $result.Output -eq '' -and $result.ErrorOutput -match 'stage_inspect_cli:') 'Truncated archive emitted success or partial output'
    }
    foreach ($offset in @(4, 76)) {
        $malformed = [byte[]]$data.Clone()
        $value = if ($offset -eq 4) { 48 } else { 0 }
        Write-Word $malformed $offset $value
        [IO.File]::WriteAllBytes($stageInvalidPath, $malformed)
        $result = Invoke-StageInspector @($stageInvalidPath)
        Assert-Stage ($result.ExitCode -eq 1 -and $result.Output -eq '' -and $result.ErrorOutput -match 'stage_inspect_cli:') 'Malformed archive emitted success or partial output'
    }
    $help = Invoke-StageInspector @('--help')
    Assert-Stage ($help.ExitCode -eq 0 -and $help.Output -match '^usage:' -and -not $help.ErrorOutput) 'Help contract failed'
    $noArguments = Invoke-StageInspector @()
    Assert-Stage ($noArguments.ExitCode -eq 2 -and $noArguments.Output -eq '' -and $noArguments.ErrorOutput -match '^usage:') 'Missing-arguments contract failed'
    $unknown = Invoke-StageInspector @($stageValidPath, '--unknown')
    Assert-Stage ($unknown.ExitCode -eq 2 -and $unknown.Output -eq '') 'Unknown option was accepted'
    $missing = Invoke-StageInspector @((Join-Path $stageFixtureDirectory 'missing.AMB'))
    Assert-Stage ($missing.ExitCode -eq 1 -and $missing.Output -eq '' -and $missing.ErrorOutput -match 'cannot open archive') 'Missing-file contract failed'
    Assert-Stage ([Convert]::ToBase64String([IO.File]::ReadAllBytes($stageValidPath)) -ceq [Convert]::ToBase64String($data)) 'Input archive was changed'
}
finally {
    [IO.File]::Delete($stageValidPath)
    [IO.File]::Delete($stageInvalidPath)
    [IO.Directory]::Delete($stageFixtureDirectory, $false)
}
