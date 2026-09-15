#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameRoot,
    [Parameter(Mandatory = $true)][string]$OriginalExecutable,
    [ValidateSet('x64', 'Win32')][string]$Architecture = 'x64',
    [string]$BuildDirectory,
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'
$nativeRepo = Split-Path -Parent $PSScriptRoot
$nativeRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$nativeOriginal = (Resolve-Path -LiteralPath $OriginalExecutable).Path
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $nativeRepo "out/native-game-$Architecture" }
$nativeBuild = [IO.Path]::GetFullPath($BuildDirectory)
function Test-NativePathWithin([string]$Path, [string]$Parent) {
    $relative = [IO.Path]::GetRelativePath($Parent, $Path)
    return -not [IO.Path]::IsPathRooted($relative) -and $relative -ne '..' -and
        -not $relative.StartsWith('..' + [IO.Path]::DirectorySeparatorChar, [StringComparison]::Ordinal)
}
if (-not (Test-NativePathWithin $nativeBuild $nativeRepo) -or $nativeBuild -eq $nativeRepo) {
    throw 'The native build directory must be inside this repository and ignored by Git.'
}
& git -C $nativeRepo check-ignore --quiet -- $nativeBuild
if ($LASTEXITCODE -ne 0) { throw 'The native build directory must be ignored by Git.' }
$nativeInputs = @($nativeOriginal)
foreach ($relative in @('G_ZONE1/MAP/ZONE1_M.AMB', 'G_ZONE1/MAP/ZONE1_T.AMB',
                        'G_ZONE1/MAP/ZONE11_MAP.AMB', 'NNSTDSHADER/SHADER.AMB',
                        'G_COM/SETTING/GM_SETTING_LIGHT.AMB')) {
    $nativeInputs += (Resolve-Path -LiteralPath (Join-Path $nativeRoot $relative)).Path
}
foreach ($inputPath in $nativeInputs) {
    if (Test-NativePathWithin $inputPath $nativeBuild) {
        throw 'The native build directory must be separate from original executable and asset inputs.'
    }
}
$nativeBefore = @($nativeInputs | ForEach-Object {
    @{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
})

if (-not ('NativeGameResources' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

public static class NativeGameResources
{
    private delegate bool ResourceNameCallback(IntPtr module, IntPtr type, IntPtr name, IntPtr argument);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool EnumResourceNamesW(IntPtr module, IntPtr type, ResourceNameCallback callback, IntPtr argument);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetShortPathNameW(string path, System.Text.StringBuilder buffer, uint size);

    public static string ShortPath(string path)
    {
        var buffer = new System.Text.StringBuilder(32768);
        uint count = GetShortPathNameW(path, buffer, (uint)buffer.Capacity);
        if (count == 0 || count >= buffer.Capacity) throw new Win32Exception(Marshal.GetLastWin32Error());
        return buffer.ToString();
    }

    private static byte[] ReadResource(IntPtr module, IntPtr name, int type)
    {
        IntPtr resource = FindResourceW(module, name, new IntPtr(type));
        if (resource == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        uint size = SizeofResource(module, resource);
        IntPtr data = LockResource(LoadResource(module, resource));
        if (size == 0 || size > 16777216 || data == IntPtr.Zero) throw new InvalidDataException("Invalid icon resource");
        byte[] bytes = new byte[(int)size];
        Marshal.Copy(data, bytes, 0, bytes.Length);
        return bytes;
    }

    public static byte[] ReadIcon(string executable)
    {
        IntPtr module = LoadLibraryExW(executable, IntPtr.Zero, 0x22);
        if (module == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            byte[] group = null;
            Exception failure = null;
            ResourceNameCallback callback = (handle, type, name, argument) => {
                if (group == null && failure == null) {
                    try { group = ReadResource(handle, name, 14); }
                    catch (Exception error) { failure = error; }
                }
                return true;
            };
            if (!EnumResourceNamesW(module, new IntPtr(14), callback, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            GC.KeepAlive(callback);
            if (failure != null) throw failure;
            if (group == null || group.Length < 6 || BitConverter.ToUInt16(group, 0) != 0 ||
                BitConverter.ToUInt16(group, 2) != 1) throw new InvalidDataException("No original icon group");
            int count = BitConverter.ToUInt16(group, 4);
            if (count == 0 || count > 256 || group.Length != 6 + count * 14)
                throw new InvalidDataException("Invalid original icon directory");
            var images = new List<byte[]>();
            for (int index = 0; index < count; ++index) {
                byte[] image = ReadResource(module, new IntPtr(BitConverter.ToUInt16(group, 6 + index * 14 + 12)), 3);
                if (image.Length != BitConverter.ToUInt32(group, 6 + index * 14 + 8))
                    throw new InvalidDataException("Icon resource length differs from its directory");
                images.Add(image);
            }
            using (var output = new MemoryStream())
            using (var writer = new BinaryWriter(output)) {
                writer.Write(group, 0, 6);
                uint offset = checked((uint)(6 + count * 16));
                for (int index = 0; index < count; ++index) {
                    writer.Write(group, 6 + index * 14, 12);
                    writer.Write(offset);
                    offset = checked(offset + (uint)images[index].Length);
                }
                foreach (byte[] image in images) writer.Write(image);
                return output.ToArray();
            }
        }
        finally { FreeLibrary(module); }
    }
}
'@
}

[void][IO.Directory]::CreateDirectory($nativeBuild)
$nativeIcon = Join-Path $nativeBuild 'original-icon.ico'
[IO.File]::WriteAllBytes($nativeIcon, [NativeGameResources]::ReadIcon($nativeOriginal))
$nativeSourceShort = [NativeGameResources]::ShortPath((Join-Path $nativeRepo 'src/Sonic4Episode2.Native'))
$nativeBuildShort = [NativeGameResources]::ShortPath($nativeBuild)
$nativeIconShort = [NativeGameResources]::ShortPath($nativeIcon)
& cmake -S $nativeSourceShort -B $nativeBuildShort -G 'Visual Studio 17 2022' -A $Architecture `
    -DBUILD_TESTING=ON -DSONIC4EP2_NATIVE_BUILD_TOOLS=ON -DSONIC4EP2_NATIVE_BUILD_GAME=ON `
    "-DSONIC4EP2_NATIVE_GAME_ICON=$nativeIconShort"
if ($LASTEXITCODE -ne 0) { throw 'Native game configuration failed' }
if (-not $PrepareOnly) {
    & cmake --build $nativeBuildShort --config Release --target sonic4ep2_game --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Native game build failed' }
}
foreach ($inputRecord in $nativeBefore) {
    if ((Get-FileHash -LiteralPath $inputRecord.path -Algorithm SHA256).Hash -ne $inputRecord.sha256) {
        throw "Original input changed during build: $($inputRecord.path)"
    }
}
if ($PrepareOnly) {
    Write-Output "Prepared native build directory: $nativeBuild"
    return
}
$nativeOutput = Join-Path $nativeBuild 'Release'
if (-not (Test-Path -LiteralPath (Join-Path $nativeOutput 'Sonic-Decomp.exe') -PathType Leaf)) {
    throw 'The native build completed without the expected Sonic-Decomp.exe.'
}
[void][IO.Directory]::CreateDirectory($nativeOutput)
[IO.File]::WriteAllText((Join-Path $nativeOutput 'game-root.txt'), $nativeRoot, [Text.UTF8Encoding]::new($false))
$nativeManifest = @{
    architecture = $Architecture
    executable = 'Sonic-Decomp.exe'
    mode = 'first-act stage inspection; gameplay and complete rendering remain WIP'
    original_inputs = $nativeBefore
    icon_sha256 = (Get-FileHash -LiteralPath $nativeIcon -Algorithm SHA256).Hash
}
$nativeManifest.executable_sha256 = (Get-FileHash -LiteralPath (Join-Path $nativeOutput 'Sonic-Decomp.exe') -Algorithm SHA256).Hash
[IO.File]::WriteAllText((Join-Path $nativeOutput 'build-inputs.json'),
    ($nativeManifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
Write-Output (Join-Path $nativeOutput 'Sonic-Decomp.exe')
