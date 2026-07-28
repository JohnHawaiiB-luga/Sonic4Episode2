using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class MaterialBlendTests
{
    private const string Root =
        @"C:\Users\DavidErikGarciaArena\Downloads\Sonic 4 - Episode 2 (Beta 8)\Sonic 4 - Episode 2 (Beta 8)";

    [Fact]
    public void TheGodrayMaterialIsAdditive()
    {
        // Read against the installed game: the godray glows, so its material must
        // decode as additive where the sky's is alpha.
        string path = Path.Combine(Root, "G_ZONE1", "MAPFAR", "EP2_MAPFAR_ZONE1.AMB");
        if (!File.Exists(path)) return;

        var outer = AmbArchive.Load(path);
        var models = outer.OpenNested(outer.Entries.First(
            e => e.Name.EndsWith("_MDL.AMB", StringComparison.OrdinalIgnoreCase)));

        MaterialBlend BlendOf(string suffix)
        {
            var entry = models.Entries.First(
                e => e.Name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase));
            var model = NnModel.Load(models.Read(entry))!;
            return model.Materials[0].Blend;
        }

        Assert.Equal(MaterialBlend.Additive, BlendOf("Z1_GODRAY.ZNO"));
        Assert.Equal(MaterialBlend.Alpha, BlendOf("Z1_SKY.ZNO"));
    }

    [Fact]
    public void AdditiveTrianglesGetTheirOwnBatchKey()
    {
        Assert.True(StageBatch.IsAdditive("+SHINE.DDS"));
        Assert.False(StageBatch.IsAdditive("STONE.DDS"));
        Assert.Equal("SHINE.DDS", StageBatch.TextureOf("+SHINE.DDS"));
        Assert.Equal("STONE.DDS", StageBatch.TextureOf("STONE.DDS"));
    }
}
