using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

/// <summary>
/// These run against the installed game, because the thing worth checking is
/// that real skinned models decode — a synthetic buffer would only test the
/// arithmetic I already wrote.
/// </summary>
public class SkinningTests
{
    private const string Root =
        @"C:\Users\DavidErikGarciaArena\Downloads\Sonic 4 - Episode 2 (Beta 8)\Sonic 4 - Episode 2 (Beta 8)";

    private static NnModel? Sonic()
    {
        string path = Path.Combine(Root, "G_COM", "PLY", "SON_MDL.AMB");
        if (!File.Exists(path)) return null;
        var archive = AmbArchive.Load(path);
        foreach (var entry in archive.Entries)
            if (entry.Name.EndsWith("SON_MODEL.ZNO", StringComparison.OrdinalIgnoreCase))
                return NnModel.Load(archive.Read(entry));
        return null;
    }

    [Fact]
    public void TheFormatBitsSayWhatTheyMean()
    {
        Assert.Equal(0x01000u, (uint)VertexFormat.Weight1);
        Assert.Equal(0x02000u, (uint)VertexFormat.Weight2);
        Assert.Equal(0x04000u, (uint)VertexFormat.Weight3);
        Assert.Equal(0x00400u, (uint)VertexFormat.BlendIndices);
    }

    [Fact]
    public void SonicsVerticesCarryThreeWeightsAndPackedIndices()
    {
        var model = Sonic();
        if (model is null) return;                 // no installed game, nothing to check

        var list = model.VertexLists[0];
        Assert.True(list.IsSkinned);
        Assert.Equal(3, list.WeightCount);
        Assert.True(list.HasBlendIndices);

        // Position 12, three weights 12, indices 4, normal 12, texcoords 8.
        Assert.Equal(48, list.Stride);
        Assert.Equal(12, list.WeightOffset);
        Assert.Equal(24, list.BlendIndexOffset);
    }

    [Fact]
    public void EveryWeightSetSumsToOne()
    {
        var model = Sonic();
        if (model is null) return;

        var list = model.VertexLists[0];
        var weights = new float[3];
        int matched = 0;
        for (int i = 0; i < list.Count; i++)
        {
            list.ReadWeights(i, weights);
            if (Math.Abs(weights[0] + weights[1] + weights[2] - 1f) < 0.001f) matched++;
        }
        Assert.Equal(list.Count, matched);
    }

    [Fact]
    public void IndicesArePaletteRelativeAndSmall()
    {
        var model = Sonic();
        if (model is null) return;

        var list = model.VertexLists[0];
        int biggest = 0;
        for (int i = 0; i < list.Count; i++)
        {
            var (a, b, c, d) = list.BlendIndices(i);
            biggest = Math.Max(biggest, Math.Max(Math.Max(a, b), Math.Max(c, d)));
        }
        // Far below the model's 109 nodes, so they index a palette rather than
        // the node array directly.
        Assert.True(biggest < 64, $"largest index {biggest}");
        Assert.True(biggest < model.Nodes.Count);
    }

    [Fact]
    public void AListWithoutIndicesReportsNoOffset()
    {
        var model = Sonic();
        if (model is null) return;

        foreach (var list in model.VertexLists)
            if (!list.HasBlendIndices)
                Assert.Equal(-1, list.BlendIndexOffset);
    }

    [Fact]
    public void TheRingIsNotSkinned()
    {
        string path = Path.Combine(Root, "G_COM", "RING", "RING_MDL.AMB");
        if (!File.Exists(path)) return;

        var archive = AmbArchive.Load(path);
        foreach (var entry in archive.Entries)
        {
            if (!entry.Name.EndsWith(".ZNO", StringComparison.OrdinalIgnoreCase)) continue;
            var model = NnModel.Load(archive.Read(entry))!;
            Assert.All(model.VertexLists, v => Assert.False(v.IsSkinned));
        }
    }
}
