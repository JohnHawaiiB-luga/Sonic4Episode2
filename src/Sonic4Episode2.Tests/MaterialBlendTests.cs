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
        var glow = new MaterialKey(new MaterialTextures("SHINE.DDS", null, null, null),
                                   MaterialBlend.Additive, MaterialKey.White);
        var solid = MaterialKey.FromBase("SHINE.DDS");

        Assert.True(glow.IsAdditive);
        Assert.False(solid.IsAdditive);

        // Same texture, different blend, so they must not share a batch.
        Assert.NotEqual(glow, solid);
        Assert.Equal("SHINE.DDS", glow.Base);
    }

    [Fact]
    public void MaterialsWithDifferentTextureSetsGetDifferentBatches()
    {
        // The whole point of the rekeying: a material that adds an environment
        // map is a different draw from the same base map on its own, so the env
        // stage can no longer be silently dropped into the base batch.
        var plain = MaterialKey.FromBase("METAL.DDS");
        var reflective = new MaterialKey(
            new MaterialTextures("METAL.DDS", "METAL_ENV.DDS", null, null),
            MaterialBlend.Alpha, MaterialKey.White);

        Assert.NotEqual(plain, reflective);
        Assert.False(plain.IsMultiTexture);
        Assert.True(reflective.IsMultiTexture);
        Assert.Equal(2, reflective.Textures.Count);

        // Value equality, so two identical materials do share one batch.
        Assert.Equal(reflective, new MaterialKey(
            new MaterialTextures("METAL.DDS", "METAL_ENV.DDS", null, null),
            MaterialBlend.Alpha, MaterialKey.White));
    }
}
