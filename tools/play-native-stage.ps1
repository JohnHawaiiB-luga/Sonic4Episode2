#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameRoot,
    [switch]$PrepareOnly,
    [switch]$Smoke,
    [switch]$RestartSmoke,
    [string]$Screenshot
)

$ErrorActionPreference = 'Stop'
if ($Smoke -and $RestartSmoke) { throw '-Smoke and -RestartSmoke cannot be combined' }
$previewRepo = Split-Path -Parent $PSScriptRoot
$previewRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$previewOutput = Join-Path $previewRepo 'out/native-preview'
$previewModels = Join-Path $previewRoot 'G_ZONE1/MAP/ZONE1_M.AMB'
$previewMap = Join-Path $previewRoot 'G_ZONE1/MAP/ZONE11_MAP.AMB'
$modelHash = (Get-FileHash -LiteralPath $previewModels -Algorithm SHA256).Hash
$mapHash = (Get-FileHash -LiteralPath $previewMap -Algorithm SHA256).Hash
if ($Screenshot) { $Screenshot = [IO.Path]::GetFullPath($Screenshot) }

Push-Location -LiteralPath $previewRepo
try {
    & cmake -S src/Sonic4Episode2.Native -B out/native -G 'Visual Studio 17 2022' -A Win32 -DBUILD_TESTING=ON -DSONIC4EP2_NATIVE_BUILD_TOOLS=ON
    if ($LASTEXITCODE -ne 0) { throw 'Native configuration failed' }
    & cmake --build out/native --config Release --target stage_scene_cli --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Native scene exporter build failed' }
    & dotnet build src/Sonic4Episode2.Desktop/Sonic4Episode2.Desktop.csproj -c Release
    if ($LASTEXITCODE -ne 0) { throw 'Desktop preview build failed' }

    [void][IO.Directory]::CreateDirectory($previewOutput)
    $layers = @()
    foreach ($layer in @('ZONE11_A', 'ZONE11_B')) {
        $depth = if ($layer -eq 'ZONE11_A') { '32' } else { '0' }
        $json = & './out/native/Release/stage_scene_cli.exe' $previewModels $previewMap $layer '24' '0' '0' $depth
        if ($LASTEXITCODE -ne 0) { throw "Native export failed for $layer" }
        $layerPath = Join-Path $previewOutput "$layer.json"
        [IO.File]::WriteAllText($layerPath, $json, [Text.UTF8Encoding]::new($false))
        $layers += @{ file = "$layer.json"; sha256 = (Get-FileHash -LiteralPath $layerPath -Algorithm SHA256).Hash }
    }
    if ((Get-FileHash -LiteralPath $previewModels -Algorithm SHA256).Hash -ne $modelHash -or
        (Get-FileHash -LiteralPath $previewMap -Algorithm SHA256).Hash -ne $mapHash) {
        throw 'Stage inputs changed during export'
    }
    $manifestPath = Join-Path $previewOutput 'manifest.json'
    $manifest = @{
        format = 'pc-stage-preview-v1'
        act = 'G_ZONE1/MAP/ZONE11_MAP.AMB'
        model_archive = 'G_ZONE1/MAP/ZONE1_M.AMB'
        model_sha256 = $modelHash
        map_sha256 = $mapHash
        layers = $layers
    } | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText($manifestPath, $manifest, [Text.UTF8Encoding]::new($false))
    $previewExecutable = Join-Path $previewRepo 'src/Sonic4Episode2.Desktop/bin/Release/net8.0/Sonic4Episode2.Desktop.exe'
    & $previewExecutable $previewRoot 'G_ZONE1/MAP/ZONE11_MAP.AMB' '--native-scenes' $manifestPath '--validate-native-scenes'
    if ($LASTEXITCODE -ne 0) { throw 'Native preview validation failed' }
    Write-Host "Prepared first-act preview: $manifestPath"
    if (-not $PrepareOnly) {
        $previewArguments = @($previewRoot, 'G_ZONE1/MAP/ZONE11_MAP.AMB', '--native-scenes', $manifestPath)
        if ($Smoke) { $previewArguments += '--playback-smoke' }
        if ($RestartSmoke) { $previewArguments += '--restart-smoke' }
        if ($Screenshot) { $previewArguments += @('--screenshot', $Screenshot) }
        if ($Smoke -or $RestartSmoke) {
            $startInfo = [Diagnostics.ProcessStartInfo]::new($previewExecutable)
            $startInfo.UseShellExecute = $false
            $startInfo.CreateNoWindow = $true
            $startInfo.RedirectStandardOutput = $true
            $startInfo.RedirectStandardError = $true
            $startInfo.WorkingDirectory = $previewRepo
            foreach ($argument in $previewArguments) { [void]$startInfo.ArgumentList.Add($argument) }
            $process = [Diagnostics.Process]::Start($startInfo)
            $outputTask = $process.StandardOutput.ReadToEndAsync()
            $errorTask = $process.StandardError.ReadToEndAsync()
            try {
                $deadline = [DateTime]::UtcNow.AddSeconds(90)
                while (-not $process.WaitForExit(1000)) {
                    if ([DateTime]::UtcNow -ge $deadline) {
                        $process.Kill($true)
                        $process.WaitForExit()
                        throw 'Preview smoke exceeded 90 seconds'
                    }
                }
                if ($process.ExitCode -ne 0) { throw "Preview smoke exited with code $($process.ExitCode)" }
            }
            finally {
                if ($process.HasExited) {
                    Write-Output $outputTask.GetAwaiter().GetResult()
                    $errorOutput = $errorTask.GetAwaiter().GetResult()
                    if ($errorOutput) { [Console]::Error.Write($errorOutput) }
                }
                $process.Dispose()
            }
        }
        else {
            & $previewExecutable @previewArguments
            if ($LASTEXITCODE -ne 0) { throw "Desktop preview exited with code $LASTEXITCODE" }
        }
    }
}
finally {
    Pop-Location
}
