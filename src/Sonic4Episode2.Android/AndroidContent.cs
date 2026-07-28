using Android.Content.Res;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Android;

/// <summary>
/// Serves game data on Android, from shared storage or from the APK's assets.
/// </summary>
/// <remarks>
/// The game's data is several gigabytes — far past what an APK can carry — so the
/// normal case is a sideloaded copy on shared storage, and <see cref="Root"/>
/// points at it. Assets are supported too because a cut-down build for one act is
/// a plausible thing to want, and because it costs a dozen lines.
/// <para>
/// <see cref="AssetManager"/> cannot enumerate recursively the way a filesystem
/// can, so the asset path lists a directory's immediate children and filters —
/// which is exactly what <see cref="IContentSource.List"/> promises and no more.
/// </para>
/// </remarks>
public sealed class AndroidContent : IContentSource
{
    private readonly AssetManager? _assets;
    private readonly string? _root;

    /// <summary>Reads a sideloaded copy of the game from shared storage.</summary>
    public AndroidContent(string root) => _root = root;

    /// <summary>Reads game data packed into the APK.</summary>
    public AndroidContent(AssetManager assets) => _assets = assets;

    /// <summary>Where this reads from, for diagnostics.</summary>
    public string Root => _root ?? "(apk assets)";

    private string Resolve(string path) =>
        _root is null ? path : Path.Combine(_root, path.Replace('/', Path.DirectorySeparatorChar));

    public bool Exists(string path)
    {
        if (_root is not null) return File.Exists(Resolve(path));
        try
        {
            using var stream = _assets!.Open(path);
            return true;
        }
        catch (Java.IO.FileNotFoundException)
        {
            return false;
        }
    }

    public byte[] Read(string path)
    {
        if (_root is not null) return File.ReadAllBytes(Resolve(path));

        using var stream = _assets!.Open(path);
        using var buffer = new MemoryStream();
        stream.CopyTo(buffer);
        return buffer.ToArray();
    }

    public IEnumerable<string> List(string directory, string suffix)
    {
        if (_root is not null)
        {
            string resolved = Resolve(directory);
            if (!Directory.Exists(resolved)) yield break;

            foreach (string file in Directory.EnumerateFiles(resolved))
            {
                if (!file.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)) continue;
                string relative = Path.GetRelativePath(_root, file);
                yield return relative.Replace(Path.DirectorySeparatorChar, '/');
            }
            yield break;
        }

        string[]? names = _assets!.List(directory);
        if (names is null) yield break;
        foreach (string name in names)
        {
            if (!name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)) continue;
            yield return directory.Length == 0 ? name : $"{directory}/{name}";
        }
    }
}
