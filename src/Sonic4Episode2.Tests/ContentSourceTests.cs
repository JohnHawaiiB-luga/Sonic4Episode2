using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

/// <summary>
/// A content source backed by a dictionary — what an Android head would look
/// like, minus the asset manager.
/// </summary>
file sealed class MemoryContent(params string[] paths) : IContentSource
{
    private readonly HashSet<string> _paths = new(paths, StringComparer.OrdinalIgnoreCase);

    public bool Exists(string path) => _paths.Contains(path);

    public byte[] Read(string path) =>
        _paths.Contains(path) ? [1, 2, 3] : throw new FileNotFoundException(path);

    public IEnumerable<string> List(string directory, string suffix) =>
        _paths.Where(p => p.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)
                          && p.LastIndexOf('/') == directory.Length
                          && p.StartsWith(directory, StringComparison.OrdinalIgnoreCase));
}

public class ContentSourceTests
{
    [Fact]
    public void TilesetLookupWorksWithoutAFilesystem()
    {
        var content = new MemoryContent(
            "G_ZONE1/MAP/ZONE11_MAP.AMB",
            "G_ZONE1/MAP/ZONE1_M.AMB");

        Assert.Equal("G_ZONE1/MAP/ZONE1_M.AMB",
                     GameEngine.FindTileset("G_ZONE1/MAP/ZONE11_MAP.AMB", content));
    }

    [Fact]
    public void ATilesetLetterIsPreferredOverThePlainForm()
    {
        var content = new MemoryContent(
            "G_ZONE2/MAP/ZONE21A_MAP.AMB",
            "G_ZONE2/MAP/ZONE2A_M.AMB",
            "G_ZONE2/MAP/ZONE2_M.AMB");

        Assert.Equal("G_ZONE2/MAP/ZONE2A_M.AMB",
                     GameEngine.FindTileset("G_ZONE2/MAP/ZONE21A_MAP.AMB", content));
    }

    [Fact]
    public void AMissingTilesetIsNullRatherThanAThrow()
    {
        var content = new MemoryContent("G_ZONE9/MAP/ZONE91_MAP.AMB");
        Assert.Null(GameEngine.FindTileset("G_ZONE9/MAP/ZONE91_MAP.AMB", content));
    }

    [Fact]
    public void ListFindsAttributeArchivesBesideAnAct()
    {
        var content = new MemoryContent(
            "G_ZONE1/MAP/ZONE11_MAP.AMB",
            "G_ZONE1/MAP/ZONE1_ATTR.AMB");

        Assert.Equal(["G_ZONE1/MAP/ZONE1_ATTR.AMB"],
                     content.List("G_ZONE1/MAP", "_ATTR.AMB"));
    }

    [Fact]
    public void FileSystemContentSpeaksForwardSlashesOnEveryPlatform()
    {
        string root = Path.Combine(Path.GetTempPath(),
                                   "s4e2-content-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "G_ZONE1", "MAP"));
        File.WriteAllBytes(Path.Combine(root, "G_ZONE1", "MAP", "ZONE1_ATTR.AMB"),
                           [7, 7, 7]);
        try
        {
            var content = new FileSystemContent(root);

            Assert.True(content.Exists("G_ZONE1/MAP/ZONE1_ATTR.AMB"));
            Assert.Equal([7, 7, 7], content.Read("G_ZONE1/MAP/ZONE1_ATTR.AMB"));

            // What List hands back must be feedable straight back into Read, with
            // no platform separator in it.
            string listed = Assert.Single(content.List("G_ZONE1/MAP", "_ATTR.AMB"));
            const char backslash = (char)92;
            Assert.DoesNotContain(listed, c => c == backslash);
            Assert.Equal([7, 7, 7], content.Read(listed));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void ListingAMissingDirectoryIsEmptyRatherThanAThrow()
    {
        var content = new FileSystemContent(Path.GetTempPath());
        Assert.Empty(content.List("no/such/place", ".AMB"));
    }
}
