#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$GameRoot
)

$ErrorActionPreference = 'Stop'
$previewRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outRoot = Join-Path $previewRepo 'out'
$ownershipFile = 'package-files.json'

function Assert-OutputDirectory([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (-not $absolute.StartsWith($outRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package directory escapes the output root: $absolute"
    }
    $ancestor = $absolute
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Package path must use normal directories: $ancestor"
            }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    return $absolute
}

function Get-PackageInventory([string]$Path) {
    $root = Assert-OutputDirectory $Path
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($root)
    $directories = [Collections.Generic.List[string]]::new()
    $files = [Collections.Generic.List[object]]::new()
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Package contains a reparse point: $($item.FullName)"
            }
            $relative = [IO.Path]::GetRelativePath($root, $item.FullName).Replace('\', '/')
            if ($item.PSIsContainer) {
                $directories.Add($relative)
                $pending.Push($item.FullName)
            }
            elseif ($relative -ne $ownershipFile) {
                $files.Add([ordered]@{ path = $relative; sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash })
            }
        }
    }
    return [ordered]@{
        format = 'sonic4episode2-package-files-v1'
        directories = @($directories | Sort-Object)
        files = @($files | Sort-Object { $_.path })
    }
}

function Test-PackageOwnership([string]$Path) {
    try {
        $inventory = Get-PackageInventory $Path
        $manifest = Get-Item -LiteralPath (Join-Path $Path $ownershipFile) -Force
        if ($manifest.Length -gt 262144) { return $false }
        $recorded = Get-Content -LiteralPath $manifest.FullName -Raw | ConvertFrom-Json -AsHashtable
        return ($inventory | ConvertTo-Json -Depth 5 -Compress) -ceq ($recorded | ConvertTo-Json -Depth 5 -Compress)
    }
    catch { return $false }
}

function Move-Package([string]$Source, [string]$Destination) {
    $sourcePath = Assert-OutputDirectory $Source
    $destinationPath = Assert-OutputDirectory $Destination
    if (Test-Path -LiteralPath $destinationPath) { throw "Package destination already exists: $destinationPath" }
    Move-Item -LiteralPath $sourcePath -Destination $destinationPath
}

function Test-StagedPreview([string]$Path) {
    $startInfo = [Diagnostics.ProcessStartInfo]::new((Join-Path $Path 'Sonic4Episode2.Desktop.exe'))
    $startInfo.WorkingDirectory = $Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    [void]$startInfo.ArgumentList.Add('--validate-native-scenes')
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(90000)) {
            $process.Kill($true)
            [void]$process.WaitForExit(5000)
            throw 'Staged native-scene validation exceeded 90 seconds'
        }
        $output = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Staged validation exited $($process.ExitCode): $output $errors" }
        Write-Output $output.TrimEnd()
    }
    finally { $process.Dispose() }
}

$gameRootPath = (Resolve-Path -LiteralPath $GameRoot).Path
if (-not [IO.Directory]::Exists($gameRootPath)) { throw "GameRoot is not a directory: $gameRootPath" }
$playable = Assert-OutputDirectory (Join-Path $outRoot 'playable')
$sceneOutput = Assert-OutputDirectory (Join-Path $outRoot 'native-preview')
& (Join-Path $PSScriptRoot 'play-native-stage.ps1') -GameRoot $gameRootPath -PrepareOnly
if ($LASTEXITCODE -ne 0) { throw 'Native preview preparation failed' }

$stage = Assert-OutputDirectory (Join-Path $outRoot ".playable-stage-$([guid]::NewGuid().ToString('N'))")
if (Test-Path -LiteralPath $stage) { throw "Staging directory already exists: $stage" }
[void][IO.Directory]::CreateDirectory($stage)
$backup = $null
try {
    $project = Join-Path $previewRepo 'src/Sonic4Episode2.Desktop/Sonic4Episode2.Desktop.csproj'
    & dotnet publish $project -c Release -r win-x64 --self-contained true '-p:PlayablePreview=true' '-p:PublishSingleFile=false' '-p:PublishTrimmed=false' -o $stage
    if ($LASTEXITCODE -ne 0) { throw 'Playable preview publish failed' }
    $scenes = Join-Path $stage 'native-preview'
    if (Test-Path -LiteralPath $scenes) { throw 'Fresh package unexpectedly contains native-preview' }
    [void][IO.Directory]::CreateDirectory($scenes)
    foreach ($name in @('manifest.json', 'ZONE11_A.json', 'ZONE11_B.json')) {
        $source = Join-Path $sceneOutput $name
        $destination = Join-Path $scenes $name
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
            throw "Native scene copy does not match: $name"
        }
    }
    $configuration = [ordered]@{
        format = 'sonic4episode2-playable-preview-v1'
        gameRoot = $gameRootPath
        nativeManifest = 'native-preview/manifest.json'
    } | ConvertTo-Json
    [IO.File]::WriteAllText((Join-Path $stage 'playable-preview.json'), $configuration, [Text.UTF8Encoding]::new($false))
    Test-StagedPreview $stage
    $inventory = Get-PackageInventory $stage | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText((Join-Path $stage $ownershipFile), $inventory, [Text.UTF8Encoding]::new($false))

    $buildConfiguration = [ordered]@{
        format = 'sonic4episode2-playable-preview-v1'
        gameRoot = $gameRootPath
        nativeManifest = Join-Path $playable 'native-preview/manifest.json'
    } | ConvertTo-Json
    $buildOutput = Join-Path $previewRepo 'src/Sonic4Episode2.Desktop/bin/Release/net8.0'
    $buildDirectories = @($buildOutput, (Join-Path $buildOutput 'win-x64'))
    foreach ($directory in $buildDirectories) {
        [IO.File]::WriteAllText((Join-Path $directory 'playable-preview.json'), $buildConfiguration, [Text.UTF8Encoding]::new($false))
    }

    if (Test-Path -LiteralPath $playable) {
        $backup = Join-Path $outRoot ".playable-backup-$([guid]::NewGuid().ToString('N'))"
        Move-Package $playable $backup
    }
    try { Move-Package $stage $playable }
    catch {
        if ($backup -and -not (Test-Path -LiteralPath $playable)) { Move-Package $backup $playable }
        throw
    }
    $stage = $null
    foreach ($directory in $buildDirectories) { Test-StagedPreview $directory }
    if ($backup) {
        if (Test-PackageOwnership $backup) {
            try {
                [void](Assert-OutputDirectory $backup)
                Remove-Item -LiteralPath $backup -Recurse -Force
            }
            catch { Write-Warning "Prior package retained at $($backup): $($_.Exception.Message)" }
        }
        else { Write-Warning "Prior package contains unrecognized or changed files; retained at $backup" }
    }
    Write-Output "Playable preview prepared: $(Join-Path $playable 'Sonic4Episode2.Desktop.exe')"
}
finally {
    if ($stage -and (Test-Path -LiteralPath $stage)) {
        [void](Get-PackageInventory $stage)
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
