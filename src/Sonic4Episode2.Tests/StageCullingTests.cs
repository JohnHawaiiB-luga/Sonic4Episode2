using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

/// <summary>
/// The column bookkeeping the renderer culls with.
/// </summary>
/// <remarks>
/// Culling is only safe if every triangle is filed under the column its tile was
/// placed in — a triangle filed too far left or right vanishes when the camera
/// reaches it. These pin that contract on the assembler side; the renderer side
/// is checked by rendering with culling on and off and diffing, which comes out
/// pixel-identical.
/// </remarks>
public class StageCullingTests
{
    private static TileMesh OneTriangle() => new()
    {
        Positions = [0, 0, 0, 10, 0, 0, 0, 10, 0],
        TexCoords = [0, 0, 1, 0, 0, 1],
        Normals = [0, 0, 1, 0, 0, 1, 0, 0, 1],
        Indices = [0, 1, 2],
        TriangleMaterials = [MaterialKey.FromBase("A.DDS")],
    };

    [Fact]
    public void ColumnsSplitTheStageOnWholeMultiplesOfTheWidth()
    {
        float w = StageBatch.ColumnWidth;

        Assert.Equal(0, StageBatch.Column(0f));
        Assert.Equal(0, StageBatch.Column(w - 1f));
        Assert.Equal(1, StageBatch.Column(w));
        Assert.Equal(2, StageBatch.Column(w * 2.5f));

        // Left of the origin has to floor, not truncate, or the column either
        // side of zero would collide and geometry would be culled wrongly.
        Assert.Equal(-1, StageBatch.Column(-1f));
        Assert.Equal(-1, StageBatch.Column(-w));
        Assert.Equal(-2, StageBatch.Column(-w - 1f));
    }

    [Fact]
    public void EveryTriangleIsFiledUnderTheColumnItWasPlacedIn()
    {
        var batch = new StageBatch();
        float w = StageBatch.ColumnWidth;

        batch.Add(OneTriangle(), 0f, 0f, 0f);
        batch.Add(OneTriangle(), w * 3f, 0f, 0f);
        batch.Add(OneTriangle(), w * 3f + 5f, 0f, 0f);

        var key = MaterialKey.FromBase("A.DDS");
        var columns = batch.ColumnsByMaterial[key];

        // One entry per triangle, parallel to the index triples.
        Assert.Equal(batch.IndicesByMaterial[key].Count / 3, columns.Count);
        Assert.Equal([0, 3, 3], columns);
    }

    [Fact]
    public void ColumnsStayParallelWhenMaterialsAreInterleaved()
    {
        // Two materials placed alternately across the stage: each material's
        // column list must track only its own triangles, or the spans the
        // renderer builds would address the wrong ones.
        var a = OneTriangle();
        var b = new TileMesh
        {
            Positions = a.Positions,
            TexCoords = a.TexCoords,
            Normals = a.Normals,
            Indices = a.Indices,
            TriangleMaterials = [MaterialKey.FromBase("B.DDS")],
        };

        var batch = new StageBatch();
        float w = StageBatch.ColumnWidth;
        for (int i = 0; i < 6; i++)
            batch.Add(i % 2 == 0 ? a : b, w * i, 0f, 0f);

        foreach (var pair in batch.IndicesByMaterial)
        {
            var columns = batch.ColumnsByMaterial[pair.Key];
            Assert.Equal(pair.Value.Count / 3, columns.Count);
        }

        Assert.Equal([0, 2, 4], batch.ColumnsByMaterial[MaterialKey.FromBase("A.DDS")]);
        Assert.Equal([1, 3, 5], batch.ColumnsByMaterial[MaterialKey.FromBase("B.DDS")]);
    }
}
