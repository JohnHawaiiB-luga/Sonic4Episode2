namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Where the game's data comes from.
/// </summary>
/// <remarks>
/// The desktop build reads loose files out of an installed copy. Android serves
/// them from inside the APK through an asset manager, iOS from a bundle, and a
/// browser build would fetch them — none of which are a filesystem. Everything in
/// this library that needs game data goes through here so that none of it has to
/// care.
/// <para>
/// Paths are always <c>/</c>-separated and relative to the game root, whatever
/// that means for the platform. An implementation is free to be case-insensitive;
/// the original data mixes cases and the desktop source normalises for it.
/// </para>
/// </remarks>
public interface IContentSource
{
    /// <summary>Whether a file is there.</summary>
    bool Exists(string path);

    /// <summary>The whole file. Throws if it is not there.</summary>
    byte[] Read(string path);

    /// <summary>
    /// Files directly inside a directory whose names end with
    /// <paramref name="suffix"/>, compared case-insensitively.
    /// </summary>
    /// <remarks>
    /// A suffix rather than a glob, because that is all this project needs and a
    /// glob would be one more thing every platform has to reimplement the same way.
    /// </remarks>
    IEnumerable<string> List(string directory, string suffix);
}

/// <summary>
/// An <see cref="IContentSource"/> over an installed copy of the game.
/// </summary>
public sealed class FileSystemContent : IContentSource
{
    private readonly string _root;

    public FileSystemContent(string root) => _root = root;

    /// <summary>The root this reads from, for diagnostics.</summary>
    public string Root => _root;

    private string Resolve(string path) =>
        Path.Combine(_root, path.Replace('/', Path.DirectorySeparatorChar));

    public bool Exists(string path) => File.Exists(Resolve(path));

    public byte[] Read(string path) => File.ReadAllBytes(Resolve(path));

    public IEnumerable<string> List(string directory, string suffix)
    {
        string resolved = Resolve(directory);
        if (!Directory.Exists(resolved)) yield break;

        foreach (string file in Directory.EnumerateFiles(resolved))
        {
            if (!file.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)) continue;
            // Hand back a root-relative, /-separated path, so callers never see a
            // platform separator and can feed it straight back to Read.
            string relative = Path.GetRelativePath(_root, file);
            yield return relative.Replace(Path.DirectorySeparatorChar, '/');
        }
    }
}
