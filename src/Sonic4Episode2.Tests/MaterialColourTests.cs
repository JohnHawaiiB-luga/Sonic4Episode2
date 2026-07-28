using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

/// <summary>
/// The material colour block, checked against the game's own archives. Every
/// material in the build carries exactly two RGBA colours: ambient then diffuse.
/// </summary>
public class MaterialColourTests
{
    private const string Root =
        @"C:\Users\DavidErikGarciaArena\Downloads\Sonic 4 - Episode 2 (Beta 8)\Sonic 4 - Episode 2 (Beta 8)";

    private static NnModel? Tile()
    {
        string path = Path.Combine(Root, "G_ZONE1", "MAP", "ZONE1_M.AMB");
        if (!File.Exists(path)) return null;
        var archive = AmbArchive.Load(path);
        foreach (var entry in archive.Entries)
            if (entry.Name.EndsWith("Z1_G_21DN_A.ZNO", StringComparison.OrdinalIgnoreCase))
                return NnModel.Load(archive.Read(entry));
        return null;
    }

    [Fact]
    public void MaterialsCarryAnAmbientAndADiffuse()
    {
        var model = Tile();
        if (model is null) return;                 // no installed game

        Assert.Equal(3, model.Materials.Count);

        // Diffuse is white with full alpha on this tile's first two materials -
        // "show the texture unchanged", which is the commonest case in the build.
        foreach (int i in new[] { 0, 1 })
        {
            var d = model.Materials[i].Diffuse;
            Assert.Equal(1f, d.R, precision: 3);
            Assert.Equal(1f, d.G, precision: 3);
            Assert.Equal(1f, d.B, precision: 3);
            Assert.Equal(1f, d.A, precision: 3);
        }

        // Ambient is a dark authored tint, not white, and differs per material.
        var a0 = model.Materials[0].Ambient;
        var a1 = model.Materials[1].Ambient;
        Assert.True(a0.R < 0.5f && a0.G < 0.5f && a0.B < 0.5f);
        Assert.NotEqual((a0.R, a0.G, a0.B), (a1.R, a1.G, a1.B));

        // The third carries a sub-1.0 diffuse alpha - per-material transparency.
        Assert.True(model.Materials[2].Diffuse.A < 1f);
    }

    [Fact]
    public void NormalsAreReadRatherThanAssumed()
    {
        var model = Tile();
        if (model is null) return;

        var list = model.VertexLists[0];
        var normals = new float[list.Count * 3];
        Assert.True(list.ReadNormals(normals));

        // Real normals are unit length and not all identical - the constant the
        // renderer used to feed would be both.
        int unit = 0;
        var distinct = new HashSet<(int, int, int)>();
        for (int i = 0; i < list.Count; i++)
        {
            float x = normals[i * 3], y = normals[i * 3 + 1], z = normals[i * 3 + 2];
            if (Math.Abs(MathF.Sqrt(x * x + y * y + z * z) - 1f) < 0.01f) unit++;
            distinct.Add(((int)(x * 8), (int)(y * 8), (int)(z * 8)));
        }
        Assert.Equal(list.Count, unit);
        Assert.True(distinct.Count > 1, "a tile should face more than one direction");
    }

    [Fact]
    public void TileMeshCarriesOneNormalPerVertex()
    {
        var model = Tile();
        if (model is null) return;

        var mesh = TileMesh.From(model);
        Assert.Equal(mesh.Positions.Length, mesh.Normals.Length);
    }
}
