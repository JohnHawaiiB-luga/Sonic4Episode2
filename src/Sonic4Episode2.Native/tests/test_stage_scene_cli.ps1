param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'
$sceneExecutable = (Resolve-Path -LiteralPath $Executable).Path
$sceneDirectory = [IO.Path]::GetDirectoryName($sceneExecutable)
$sceneFixtureDirectory = Join-Path $sceneDirectory ('stage-scene-cli-test-' + [Guid]::NewGuid().ToString('N'))

function Invoke-SceneExporter([string[]]$CommandArguments) {
    $quoted = foreach ($argument in $CommandArguments) {
        if ($argument.Contains('"')) { throw 'Unexpected quote in test argument' }
        '"' + $argument + '"'
    }
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $sceneExecutable
    $startInfo.Arguments = $quoted -join ' '
    $startInfo.WorkingDirectory = $sceneDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'Could not start scene exporter' }
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(10000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'Scene exporter exceeded the test deadline'
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

function Assert-Scene([bool]$Condition, [string]$Message) {
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
    $entryTableOffset = 32
    $nameTableOffset = $entryTableOffset + $Members.Count * 16
    $dataOffset = $nameTableOffset + $Members.Count * 32
    $size = $dataOffset
    foreach ($member in $Members) { $size += $member.Bytes.Length }

    $data = [byte[]]::new($size)
    Write-Tag $data 0 '#AMB'
    Write-Word $data 4 32
    Write-Word $data 8 262144
    Write-Word $data 16 $Members.Count
    Write-Word $data 20 $entryTableOffset
    Write-Word $data 24 $dataOffset
    Write-Word $data 28 $nameTableOffset
    $cursor = $dataOffset
    for ($index = 0; $index -lt $Members.Count; ++$index) {
        $member = $Members[$index]
        $entryOffset = $entryTableOffset + $index * 16
        Write-Word $data $entryOffset $cursor
        Write-Word $data ($entryOffset + 4) $member.Bytes.Length
        $member.Bytes.CopyTo($data, $cursor)
        for ($nameIndex = 0; $nameIndex -lt $member.Name.Length; ++$nameIndex) {
            $data[$nameTableOffset + $index * 32 + $nameIndex] = [byte][char]$member.Name[$nameIndex]
        }
        $cursor += $member.Bytes.Length
    }
    return ,$data
}

function New-EmptyGrid([uint16]$Width, [uint16]$Height, [byte]$CellBytes) {
    $data = [byte[]]::new(4 + $Width * $Height * $CellBytes)
    $data[0] = [byte]($Width -band 255)
    $data[1] = [byte](($Width -shr 8) -band 255)
    $data[2] = [byte]($Height -band 255)
    $data[3] = [byte](($Height -shr 8) -band 255)
    return ,$data
}

[void][IO.Directory]::CreateDirectory($sceneFixtureDirectory)
$modelsPath = Join-Path $sceneFixtureDirectory ('models ' + [char]0x00e9 + '.AMB')
$mapPath = Join-Path $sceneFixtureDirectory ('map ' + [char]0x65e5 + '.AMB')
$missingGridPath = Join-Path $sceneFixtureDirectory 'missing-grid.AMB'
$missingMpPath = Join-Path $sceneFixtureDirectory 'missing-mp.AMB'
$duplicateGridPath = Join-Path $sceneFixtureDirectory 'duplicate-grid.AMB'
$duplicateMdPath = Join-Path $sceneFixtureDirectory 'duplicate-md.AMB'
$invalidModelsPath = Join-Path $sceneFixtureDirectory 'invalid-models.AMB'
$invalidMapPath = Join-Path $sceneFixtureDirectory 'invalid-map.AMB'
$largeGridPath = Join-Path $sceneFixtureDirectory 'large-grid.AMB'
try {
    $emptyModels = New-Archive @(
        @{ Name = 'slot0.ZNO'; Bytes = [byte[]]::new(0) }
    )
    $emptyMp = New-EmptyGrid 2 1 2
    $emptyMd = New-EmptyGrid 2 1 1
    $validMap = New-Archive @(
        @{ Name = '.\.\scene.MP'; Bytes = $emptyMp },
        @{ Name = '.\.\scene.md'; Bytes = $emptyMd }
    )
    $missingGridMap = New-Archive @(
        @{ Name = 'scene.MP'; Bytes = $emptyMp }
    )
    $missingMpMap = New-Archive @(
        @{ Name = 'scene.MD'; Bytes = $emptyMd }
    )
    $duplicateGridMap = New-Archive @(
        @{ Name = 'scene.MP'; Bytes = $emptyMp },
        @{ Name = 'SCENE.mp'; Bytes = $emptyMp },
        @{ Name = 'scene.MD'; Bytes = $emptyMd }
    )
    $duplicateMdMap = New-Archive @(
        @{ Name = 'scene.MP'; Bytes = $emptyMp },
        @{ Name = 'scene.MD'; Bytes = $emptyMd },
        @{ Name = 'SCENE.md'; Bytes = $emptyMd }
    )
    [IO.File]::WriteAllBytes($modelsPath, $emptyModels)
    [IO.File]::WriteAllBytes($mapPath, $validMap)
    [IO.File]::WriteAllBytes($missingGridPath, $missingGridMap)
    [IO.File]::WriteAllBytes($missingMpPath, $missingMpMap)
    [IO.File]::WriteAllBytes($duplicateGridPath, $duplicateGridMap)
    [IO.File]::WriteAllBytes($duplicateMdPath, $duplicateMdMap)
    [IO.File]::WriteAllBytes($invalidModelsPath, [byte[]]::new(0))
    [IO.File]::WriteAllBytes($invalidMapPath, [byte[]]::new(0))
    [IO.File]::WriteAllBytes($largeGridPath, (New-Archive @(
        @{ Name = 'scene.MP'; Bytes = (New-EmptyGrid 1025 1024 2) },
        @{ Name = 'scene.MD'; Bytes = (New-EmptyGrid 1025 1024 1) }
    )))

    $modes = @(
        @{ Precision = '24'; OffsetBits = '2147483648,1069547520,3222274048' },
        @{ Precision = '53'; OffsetBits = '2147483648,1069547520,3222274048' }
    )
    foreach ($mode in $modes) {
        $result = Invoke-SceneExporter -CommandArguments @(
            $modelsPath, $mapPath, 'sCeNe', $mode.Precision, '-0', '1.5', '-2.25'
        )
        Assert-Scene ($result.ExitCode -eq 0 -and -not $result.ErrorOutput) 'Valid empty scene export failed'
        $parsed = $result.Output | ConvertFrom-Json
        Assert-Scene ($parsed.format -eq 'pc-stage-scene-v1' -and $parsed.precision -eq [int]$mode.Precision) 'Scene format or precision differs'
        Assert-Scene ($parsed.layer -ceq 'SCENE' -and $parsed.width -eq 2 -and $parsed.height -eq 1 -and $parsed.model_count -eq 1) 'Scene metadata differs'
        Assert-Scene (($parsed.offset_bits -join ',') -eq $mode.OffsetBits) 'Scene offset bits differ'
        Assert-Scene (@($parsed.instances).Count -eq 0) 'Empty scene emitted instances'
    }

    $badPrecision = Invoke-SceneExporter -CommandArguments @($modelsPath, $mapPath, 'scene', '25', '0', '0', '0')
    Assert-Scene ($badPrecision.ExitCode -eq 1 -and $badPrecision.Output -eq '' -and $badPrecision.ErrorOutput -match '^stage_scene_cli:') 'Unsupported precision emitted success or partial output'
    $badOffset = Invoke-SceneExporter -CommandArguments @($modelsPath, $mapPath, 'scene', '24', '16777216.1', '0', '0')
    Assert-Scene ($badOffset.ExitCode -eq 1 -and $badOffset.Output -eq '' -and $badOffset.ErrorOutput -match '^stage_scene_cli:') 'Out-of-range decimal offset emitted success or partial output'
    $nonDecimalOffset = Invoke-SceneExporter -CommandArguments @($modelsPath, $mapPath, 'scene', '24', 'nan', '0', '0')
    Assert-Scene ($nonDecimalOffset.ExitCode -eq 1 -and $nonDecimalOffset.Output -eq '' -and $nonDecimalOffset.ErrorOutput -match '^stage_scene_cli:') 'Non-decimal offset emitted success or partial output'
    $badLayer = Invoke-SceneExporter -CommandArguments @($modelsPath, $mapPath, 'scene!', '24', '0', '0', '0')
    Assert-Scene ($badLayer.ExitCode -eq 1 -and $badLayer.Output -eq '' -and $badLayer.ErrorOutput -match '^stage_scene_cli:') 'Invalid layer emitted success or partial output'
    $longLayer = Invoke-SceneExporter -CommandArguments @($modelsPath, $mapPath, ('A' * 65), '24', '0', '0', '0')
    Assert-Scene ($longLayer.ExitCode -eq 1 -and $longLayer.Output -eq '') 'Oversized layer name was accepted'
    $largeGrid = Invoke-SceneExporter -CommandArguments @($modelsPath, $largeGridPath, 'scene', '24', '0', '0', '0')
    Assert-Scene ($largeGrid.ExitCode -eq 1 -and $largeGrid.Output -eq '' -and $largeGrid.ErrorOutput -match 'cell limit') 'Oversized scene grid was accepted'
    $missingLayer = Invoke-SceneExporter -CommandArguments @($modelsPath, $mapPath, 'other', '24', '0', '0', '0')
    Assert-Scene ($missingLayer.ExitCode -eq 1 -and $missingLayer.Output -eq '' -and $missingLayer.ErrorOutput -match '^stage_scene_cli:') 'Missing named layer emitted success or partial output'
    $missingGrid = Invoke-SceneExporter -CommandArguments @($modelsPath, $missingGridPath, 'scene', '24', '0', '0', '0')
    Assert-Scene ($missingGrid.ExitCode -eq 1 -and $missingGrid.Output -eq '' -and $missingGrid.ErrorOutput -match '^stage_scene_cli:') 'Missing MD grid emitted success or partial output'
    $missingMp = Invoke-SceneExporter -CommandArguments @($modelsPath, $missingMpPath, 'scene', '24', '0', '0', '0')
    Assert-Scene ($missingMp.ExitCode -eq 1 -and $missingMp.Output -eq '' -and $missingMp.ErrorOutput -match '^stage_scene_cli:') 'Missing MP grid emitted success or partial output'
    $duplicateGrid = Invoke-SceneExporter -CommandArguments @($modelsPath, $duplicateGridPath, 'scene', '24', '0', '0', '0')
    Assert-Scene ($duplicateGrid.ExitCode -eq 1 -and $duplicateGrid.Output -eq '' -and $duplicateGrid.ErrorOutput -match '^stage_scene_cli:') 'Duplicate MP grid emitted success or partial output'
    $duplicateMd = Invoke-SceneExporter -CommandArguments @($modelsPath, $duplicateMdPath, 'scene', '24', '0', '0', '0')
    Assert-Scene ($duplicateMd.ExitCode -eq 1 -and $duplicateMd.Output -eq '' -and $duplicateMd.ErrorOutput -match '^stage_scene_cli:') 'Duplicate MD grid emitted success or partial output'
    $invalidModels = Invoke-SceneExporter -CommandArguments @($invalidModelsPath, $mapPath, 'scene', '24', '0', '0', '0')
    Assert-Scene ($invalidModels.ExitCode -eq 1 -and $invalidModels.Output -eq '' -and $invalidModels.ErrorOutput -match '^stage_scene_cli:') 'Malformed model archive emitted success or partial output'
    $invalidMap = Invoke-SceneExporter -CommandArguments @($modelsPath, $invalidMapPath, 'scene', '24', '0', '0', '0')
    Assert-Scene ($invalidMap.ExitCode -eq 1 -and $invalidMap.Output -eq '' -and $invalidMap.ErrorOutput -match '^stage_scene_cli:') 'Malformed map archive emitted success or partial output'

    $help = Invoke-SceneExporter -CommandArguments @('--help')
    Assert-Scene ($help.ExitCode -eq 0 -and $help.Output -match '^usage:' -and -not $help.ErrorOutput) 'Help contract failed'
    $noArguments = Invoke-SceneExporter -CommandArguments @()
    Assert-Scene ($noArguments.ExitCode -eq 2 -and $noArguments.Output -eq '' -and $noArguments.ErrorOutput -match '^usage:') 'Missing-arguments contract failed'
    $shortArguments = Invoke-SceneExporter -CommandArguments @($modelsPath, $mapPath, 'scene', '24', '0', '0')
    Assert-Scene ($shortArguments.ExitCode -eq 2 -and $shortArguments.Output -eq '' -and $shortArguments.ErrorOutput -match '^usage:') 'Wrong argument count was accepted'

    Assert-Scene ([Convert]::ToBase64String([IO.File]::ReadAllBytes($modelsPath)) -ceq [Convert]::ToBase64String($emptyModels)) 'Model input archive was changed'
    Assert-Scene ([Convert]::ToBase64String([IO.File]::ReadAllBytes($mapPath)) -ceq [Convert]::ToBase64String($validMap)) 'Map input archive was changed'
}
finally {
    if ([IO.Directory]::Exists($sceneFixtureDirectory)) {
        $resolvedFixture = (Resolve-Path -LiteralPath $sceneFixtureDirectory).Path
        $resolvedParent = (Resolve-Path -LiteralPath ([IO.Path]::GetDirectoryName($resolvedFixture))).Path
        $leaf = [IO.Path]::GetFileName($resolvedFixture)
        if (-not [string]::Equals($resolvedParent, $sceneDirectory, [StringComparison]::OrdinalIgnoreCase) -or
            -not $leaf.StartsWith('stage-scene-cli-test-', [StringComparison]::Ordinal)) {
            throw 'Refusing to remove an unexpected scene fixture directory'
        }
        [IO.Directory]::Delete($resolvedFixture, $true)
    }
}
