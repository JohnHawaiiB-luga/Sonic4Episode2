using System.Numerics;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

/// <summary>
/// The matrix palette, verified the way it was recovered: against the game's
/// own models. At the bind pose every palette slot must come out identity,
/// because each slot is <c>InverseBind · bindWorld</c> and the two are inverses
/// by construction — that one property pins the inverse-bind offset (+0x30),
/// the XYZ rotation order, the multiplication order and the unit flags all at
/// once. <c>SON_SPINMODEL</c> is the witness; <c>SON_MODEL</c> carries a helper
/// cluster whose authored init pose differs from its stored TRS (its own flags
/// say so), so it cannot serve here.
/// </summary>
public class MatrixPaletteTests
{
    private const string Root =
        @"C:\Users\DavidErikGarciaArena\Downloads\Sonic 4 - Episode 2 (Beta 8)\Sonic 4 - Episode 2 (Beta 8)";

    private static NnModel? Player(string name)
    {
        string path = Path.Combine(Root, "G_COM", "PLY", "SON_MDL.AMB");
        if (!File.Exists(path)) return null;
        var archive = AmbArchive.Load(path);
        foreach (var entry in archive.Entries)
            if (entry.Name.EndsWith(name, StringComparison.OrdinalIgnoreCase))
                return NnModel.Load(archive.Read(entry));
        return null;
    }

    [Fact]
    public void SpinModelBindPaletteIsIdentity()
    {
        var model = Player("SON_SPINMODEL.ZNO");
        if (model is null) return;                 // no installed game

        Assert.Equal(17, model.Header.MatrixPaletteCount);
        var palette = MatrixPalette.Build(model.Nodes,
            NodeTransforms.World(model.Nodes), model.Header.MatrixPaletteCount);

        foreach (var m in palette)
        {
            var d = m - Matrix4x4.Identity;
            float worst = 0f;
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    worst = MathF.Max(worst, MathF.Abs(d[r, c]));
            Assert.True(worst < 1e-3f, $"palette slot off identity by {worst}");
        }
    }

    [Fact]
    public void SonicsBoneSubsetsIndexThePalette()
    {
        var model = Player("SON_MODEL.ZNO");
        if (model is null) return;

        Assert.Equal(99, model.Header.MatrixPaletteCount);
        int weighted = 0;
        foreach (var list in model.VertexLists)
        {
            if (!list.IsSkinned) continue;
            weighted++;
            Assert.InRange(list.MatrixIndices.Count, 1, 16);
            Assert.All(list.MatrixIndices,
                slot => Assert.InRange(slot, 0, model.Header.MatrixPaletteCount - 1));
            if (!list.HasBlendIndices)
                Assert.True(list.MatrixIndices.Count <= 4,
                    "implied blend indices only reach the first four bones");
        }
        Assert.True(weighted > 0);
    }

    [Fact]
    public void PaletteSlotsAreClaimedExactlyOnce()
    {
        var model = Player("SON_MODEL.ZNO");
        if (model is null) return;

        var slots = model.Nodes.Where(n => n.MatrixIndex != -1)
                               .Select(n => (int)n.MatrixIndex)
                               .OrderBy(i => i).ToArray();
        Assert.Equal(Enumerable.Range(0, model.Header.MatrixPaletteCount), slots);
    }

    [Fact]
    public void SkinnedBindPoseKeepsTheModelInItsBox()
    {
        var model = Player("SON_SPINMODEL.ZNO");
        if (model is null) return;

        var mesh = TileMesh.Skinned(model, NodeTransforms.World(model.Nodes));
        Assert.True(mesh.Positions.Length > 0);

        // With an identity palette the skinned vertices are the stored ones, so
        // they must land inside the model's own declared bounding volume.
        float radius = model.Header.Radius * 1.01f + 0.1f;
        var centre = new Vector3(model.Header.CenterX, model.Header.CenterY,
                                 model.Header.CenterZ);
        for (int i = 0; i < mesh.Positions.Length; i += 3)
        {
            var p = new Vector3(mesh.Positions[i], mesh.Positions[i + 1],
                                mesh.Positions[i + 2]);
            Assert.True(Vector3.Distance(p, centre) <= radius,
                $"vertex {i / 3} at {p} escapes the bind-pose bounding sphere");
        }
    }

    [Fact]
    public void BuildUsesInverseBindThenWorld()
    {
        // A bone a quarter turn about Z with an inverse bind that undoes it must
        // produce an identity slot; a unit-init-matrix bone must copy its world.
        var rotated = new NnNode(0, 0, -1, -1, -1, 0, 0, 0, 0, 0, 16384, 1, 1, 1)
        {
            InverseBind = Matrix4x4.CreateRotationZ(-MathF.Tau / 4f),
        };
        var copied = new NnNode(NnNode.UnitInitMatrix, 1, 0, -1, -1,
                                3f, 0, 0, 0, 0, 0, 1, 1, 1);

        var nodes = new[] { rotated, copied };
        var world = NodeTransforms.World(nodes);
        var palette = MatrixPalette.Build(nodes, world, 2);

        var d = palette[0] - Matrix4x4.Identity;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                Assert.True(MathF.Abs(d[r, c]) < 1e-5f);
        Assert.Equal(world[1], palette[1]);
    }
}
