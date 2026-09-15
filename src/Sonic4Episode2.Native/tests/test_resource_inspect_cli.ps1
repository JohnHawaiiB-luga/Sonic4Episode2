param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'
$resourceExecutable = (Resolve-Path -LiteralPath $Executable).Path
$resourceDirectory = Split-Path -Parent $resourceExecutable
$resourceFixtureDirectory = Join-Path $resourceDirectory ('resource-cli-test-' + [Guid]::NewGuid().ToString('N'))

function Invoke-ResourceInspector([string[]]$Arguments) {
    $quoted = foreach ($argument in $Arguments) {
        if ($argument.Contains('"')) { throw 'Unexpected quote in test argument' }
        '"' + $argument + '"'
    }
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resourceExecutable
    $startInfo.Arguments = $quoted -join ' '
    $startInfo.WorkingDirectory = $resourceDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'Could not start resource inspector' }
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(10000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'Resource inspector exceeded the test deadline'
        }
        [PSCustomObject]@{
            ExitCode = $process.ExitCode
            Output = $outputTask.GetAwaiter().GetResult()
            ErrorOutput = $errorTask.GetAwaiter().GetResult()
        }
    }
    finally { $process.Dispose() }
}

function Assert-Resource([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Write-Word([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
    for ($shift = 0; $shift -lt 4; ++$shift) {
        $Buffer[$Offset + $shift] = [byte](($Value -shr ($shift * 8)) -band 255)
    }
}

function Write-Tag([byte[]]$Buffer, [int]$Offset, [string]$Tag) {
    [Text.Encoding]::ASCII.GetBytes($Tag).CopyTo($Buffer, $Offset)
}

function New-Archive([object[]]$Members) {
    $names = 32 + $Members.Count * 16
    $start = $names + $Members.Count * 32
    $size = $start
    foreach ($member in $Members) { $size += $member.Bytes.Length }
    $data = [byte[]]::new($size)
    Write-Tag $data 0 '#AMB'
    Write-Word $data 4 32
    Write-Word $data 8 262144
    Write-Word $data 16 $Members.Count
    Write-Word $data 20 32
    Write-Word $data 24 $start
    Write-Word $data 28 $names
    for ($i = 0; $i -lt $Members.Count; ++$i) {
        $member = $Members[$i]
        Write-Word $data (32 + $i * 16) $start
        Write-Word $data (36 + $i * 16) $member.Bytes.Length
        $member.Bytes.CopyTo($data, $start)
        for ($j = 0; $j -lt $member.Name.Length; ++$j) {
            $data[$names + $i * 32 + $j] = [byte][char]$member.Name[$j]
        }
        $start += $member.Bytes.Length
    }
    return ,$data
}

function New-Model([bool]$WithoutNodes = $false) {
    $data = [byte[]]::new(640)
    Write-Tag $data 0 'NZIF'
    Write-Word $data 4 24
    Write-Word $data 8 1
    Write-Word $data 12 32
    Write-Word $data 16 480
    Write-Word $data 20 512
    Write-Word $data 24 80
    Write-Word $data 28 1
    Write-Tag $data 32 'NZOB'
    Write-Word $data 36 472
    Write-Word $data 40 16
    Write-Word $data 44 3
    Write-Word $data 48 2147483648
    Write-Word $data 52 1065353216
    Write-Word $data 56 2143294004
    Write-Word $data 60 1073741824
    foreach ($offset in @(64, 72, 80, 88, 92, 100, 104)) { Write-Word $data $offset 1 }
    Write-Word $data 68 (136 - 32)
    Write-Word $data 76 (316 - 32)
    Write-Word $data 84 (412 - 32)
    Write-Word $data 96 (172 - 32)
    Write-Word $data 108 (452 - 32)
    Write-Word $data 116 32
    Write-Word $data 120 3
    Write-Word $data 176 4294901760
    Write-Word $data 180 4294967295
    Write-Word $data 184 2147483648
    Write-Word $data 188 2143294004
    Write-Word $data 192 4290772994
    if ($WithoutNodes) {
        Write-Word $data 88 0
        Write-Word $data 92 0
        Write-Word $data 96 0
        Write-Word $data 104 0
        Write-Word $data 108 0
    }
    Write-Word $data 140 (144 - 32)
    Write-Word $data 316 7
    Write-Word $data 320 (324 - 32)
    Write-Word $data 324 25
    Write-Word $data 328 194
    Write-Word $data 332 20
    Write-Word $data 336 3
    Write-Word $data 340 (352 - 32)
    for ($i = 0; $i -lt 60; ++$i) { $data[352 + $i] = [byte]$i }
    Write-Word $data 412 9
    Write-Word $data 416 (420 - 32)
    Write-Word $data 420 18448
    Write-Word $data 424 3
    Write-Word $data 428 1
    Write-Word $data 432 (440 - 32)
    Write-Word $data 436 (444 - 32)
    Write-Word $data 440 3
    ([byte[]](0, 0, 2, 0, 1, 0)).CopyTo($data, 444)
    Write-Word $data 452 5
    Write-Word $data 456 1
    Write-Word $data 460 (472 - 32)
    Write-Word $data 508 305419896
    Write-Tag $data 512 'NOF0'
    Write-Word $data 516 72
    $relocations = @(40, 68, 76, 84, 96, 108, 140, 320, 340, 416, 432, 436, 460)
    Write-Word $data 520 $relocations.Count
    for ($i = 0; $i -lt $relocations.Count; ++$i) {
        Write-Word $data (528 + $i * 4) ($relocations[$i] - 32)
    }
    Write-Tag $data 592 'NFN0'
    Write-Word $data 596 24
    Write-Tag $data 608 'synthetic.zno'
    Write-Tag $data 624 'NEND'
    Write-Word $data 628 8
    return ,$data
}

[void][IO.Directory]::CreateDirectory($resourceFixtureDirectory)
$resourceUnicodeName = 'synthetic ' + [char]0x00e9 + ' ' + [char]0x65e5 + [char]0x672c + '.AMB'
$resourceValidPath = Join-Path $resourceFixtureDirectory $resourceUnicodeName
$resourceInvalidPath = Join-Path $resourceFixtureDirectory 'invalid.AMB'
try {
    $terrain = [byte[]]::new(138)
    $terrain[0] = 3
    $terrain[2] = 2
    for ($i = 0; $i -lt 128; ++$i) { $terrain[4 + $i] = [byte](128 + $i) }
    $terrain[132] = 1
    $terrain[136] = 1
    $model = New-Model
    $noNodeModel = New-Model $true
    $rawName = 'quote"\path' + [char]10 + [char]255 + '.at'
    $data = New-Archive @(
        @{Name = $rawName; Bytes = $terrain},
        @{Name = 'triangle.zno'; Bytes = $model},
        @{Name = 'no-node.zno'; Bytes = $noNodeModel},
        @{Name = 'empty.bin'; Bytes = [byte[]]::new(0)}
    )
    Write-Word $data 44 2166572292
    Write-Word $data 60 4
    Write-Word $data 92 4294967295
    [IO.File]::WriteAllBytes($resourceValidPath, $data)
    foreach ($full in @($false, $true)) {
        $arguments = @($resourceValidPath)
        if ($full) { $arguments += '--data' }
        $result = Invoke-ResourceInspector $arguments
        Assert-Resource ($result.ExitCode -eq 0 -and -not $result.ErrorOutput) ('Valid resource archive failed: ' + $result.ErrorOutput)
        $parsed = $result.Output | ConvertFrom-Json
        Assert-Resource ($parsed.format -eq 'pc-amb32-resources' -and $parsed.entry_count -eq 4 -and $parsed.decoded_count -eq 3) 'Archive summary differs'
        Assert-Resource ($parsed.entries[0].name -ceq $rawName) 'Raw archive name was not preserved'
        Assert-Resource ($parsed.entries[3].index -eq 3 -and $parsed.entries[3].length -eq 0) 'Empty archive slot was dropped'
        Assert-Resource ($parsed.entries[0].flags -eq 2166572292 -and $parsed.entries[1].flags -eq 4 -and $parsed.entries[3].flags -eq 4294967295) 'Unsigned archive flags differ'
        $t = $parsed.entries[0].terrain
        Assert-Resource ($t.kind -eq 'attribute' -and $t.record_count -eq 2 -and $t.record_size -eq 64 -and $t.chip_count -eq 3 -and $t.record_bytes -eq 128) 'Terrain summary differs'
        $m = $parsed.entries[1].model
        Assert-Resource ($m.version -eq 3 -and $m.flags -eq 32 -and $m.material_count -eq 1 -and $m.node_count -eq 1 -and $m.texture_count -eq 0) 'Model summary differs'
        Assert-Resource (($m.bounds_bits -join ',') -eq '2147483648,1065353216,2143294004,1073741824') 'Model float bits changed'
        Assert-Resource (($m.first_node_translation_bits -join ',') -eq '2147483648,2143294004,4290772994') 'First node translation bits differ'
        $noNode = $parsed.entries[2].model
        Assert-Resource ($noNode.node_count -eq 0 -and $null -eq $noNode.first_node_translation_bits) 'No-node model did not emit a null first-node translation'
        Assert-Resource ($m.vertices.Count -eq 1 -and $m.vertices[0].format -eq 25 -and $m.vertices[0].fvf -eq 194 -and $m.vertices[0].stride -eq 20 -and $m.vertices[0].count -eq 3 -and $m.vertices[0].flags -eq 7) 'Vertex metadata differs'
        Assert-Resource ($m.primitives.Count -eq 1 -and $m.primitives[0].mode -eq 18448 -and $m.primitives[0].flags -eq 9 -and $m.primitives[0].index_count -eq 3) 'Primitive metadata differs'
        Assert-Resource ($m.sub_objects[0].flags -eq 5 -and $m.sub_objects[0].meshes[0].reserved -eq 305419896 -and $m.sub_objects[0].meshes[0].vertex_index -eq 0) 'Mesh binding differs'
        if ($full) {
            $terrainHex = -join (128..255 | ForEach-Object { $_.ToString('x2') })
            $vertexHex = -join (0..59 | ForEach-Object { $_.ToString('x2') })
            Assert-Resource ($t.records_hex -ceq $terrainHex -and ($t.chip_records -join ',') -eq '1,0,1') 'Terrain bytes or chip references differ'
            Assert-Resource ($m.vertices[0].bytes_hex -ceq $vertexHex -and ($m.primitives[0].indices -join ',') -eq '0,2,1') 'Geometry bytes or indices differ'
        }
        else {
            Assert-Resource (-not ($t.PSObject.Properties.Name -contains 'records_hex') -and -not ($m.vertices[0].PSObject.Properties.Name -contains 'bytes_hex')) 'Summary emitted full buffers'
        }
    }
    foreach ($length in @(0, 31, 79, ($data.Length - 1))) {
        $truncated = [byte[]]::new($length)
        [Array]::Copy($data, $truncated, $length)
        [IO.File]::WriteAllBytes($resourceInvalidPath, $truncated)
        $result = Invoke-ResourceInspector @($resourceInvalidPath)
        Assert-Resource ($result.ExitCode -eq 1 -and $result.Output -eq '' -and $result.ErrorOutput -match '^resource_inspect_cli:') 'Truncated archive emitted success or partial output'
    }
    foreach ($offset in @(0, 176, 332, 444)) {
        $badModel = [byte[]]$model.Clone()
        Write-Word $badModel $offset 65535
        $badArchive = New-Archive @(@{Name = 'ok.AT'; Bytes = $terrain}, @{Name = 'bad.ZNO'; Bytes = $badModel})
        [IO.File]::WriteAllBytes($resourceInvalidPath, $badArchive)
        $result = Invoke-ResourceInspector @($resourceInvalidPath, '--data')
        Assert-Resource ($result.ExitCode -eq 1 -and $result.Output -eq '' -and $result.ErrorOutput -match '^resource_inspect_cli:') 'Bad model after valid terrain emitted partial output'
    }
    $help = Invoke-ResourceInspector @('--help')
    Assert-Resource ($help.ExitCode -eq 0 -and $help.Output -match '^usage:' -and -not $help.ErrorOutput) 'Help contract failed'
    $noArguments = Invoke-ResourceInspector @()
    Assert-Resource ($noArguments.ExitCode -eq 2 -and $noArguments.Output -eq '' -and $noArguments.ErrorOutput -match '^usage:') 'Missing-arguments contract failed'
    $unknown = Invoke-ResourceInspector @($resourceValidPath, '--unknown')
    Assert-Resource ($unknown.ExitCode -eq 2 -and $unknown.Output -eq '') 'Unknown option was accepted'
    $missing = Invoke-ResourceInspector @((Join-Path $resourceFixtureDirectory 'missing.AMB'))
    Assert-Resource ($missing.ExitCode -eq 1 -and $missing.Output -eq '' -and $missing.ErrorOutput -match 'cannot open archive') 'Missing-file contract failed'
    Assert-Resource ([Convert]::ToBase64String([IO.File]::ReadAllBytes($resourceValidPath)) -ceq [Convert]::ToBase64String($data)) 'Input archive changed'
}
finally {
    [IO.File]::Delete($resourceValidPath)
    [IO.File]::Delete($resourceInvalidPath)
    [IO.Directory]::Delete($resourceFixtureDirectory, $false)
}
