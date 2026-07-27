using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class NodeTransformTests
{
    private static NnNode Node(short parent, float tx = 0, float ty = 0, float tz = 0,
                               int rx = 0, int ry = 0, int rz = 0,
                               float sx = 1, float sy = 1, float sz = 1) =>
        new(0, 0, parent, -1, -1, tx, ty, tz, rx, ry, rz, sx, sy, sz);

    [Fact]
    public void AFullTurnIsSixtyFiveThousandFiveHundredAndThirtySix()
    {
        Assert.Equal(65536, NnNode.RotationUnitsPerTurn);

        var quarter = Node(-1, rz: 16384);
        Assert.Equal(MathF.Tau / 4f, quarter.RotationRadians.Z, precision: 5);

        var half = Node(-1, rz: -32768);
        Assert.Equal(-MathF.PI, half.RotationRadians.Z, precision: 5);
    }

    [Fact]
    public void ARootNodesWorldMatrixIsItsLocalOne()
    {
        var nodes = new[] { Node(-1, tx: 3f, ty: 4f) };
        var world = NodeTransforms.World(nodes);
        Assert.Equal(new Vector3(3f, 4f, 0f), world[0].Translation);
    }

    [Fact]
    public void AChildIsPositionedRelativeToItsParent()
    {
        var nodes = new[] { Node(-1, ty: 10f), Node(0, ty: 5f) };
        var world = NodeTransforms.World(nodes);
        Assert.Equal(10f, world[0].Translation.Y, precision: 4);
        Assert.Equal(15f, world[1].Translation.Y, precision: 4);
    }

    [Fact]
    public void AParentsRotationCarriesToItsChild()
    {
        // Parent turned a quarter turn about Z; a child one unit along X should
        // end up one unit along Y.
        var nodes = new[] { Node(-1, rz: 16384), Node(0, tx: 1f) };
        var world = NodeTransforms.World(nodes);
        Assert.Equal(0f, world[1].Translation.X, precision: 4);
        Assert.Equal(1f, world[1].Translation.Y, precision: 4);
    }

    [Fact]
    public void AParentsScaleCarriesToItsChild()
    {
        var nodes = new[] { Node(-1, sx: 2f, sy: 2f, sz: 2f), Node(0, tx: 3f) };
        var world = NodeTransforms.World(nodes);
        Assert.Equal(6f, world[1].Translation.X, precision: 4);
    }

    [Fact]
    public void ChainsAccumulate()
    {
        var nodes = new[] { Node(-1, ty: 1f), Node(0, ty: 1f), Node(1, ty: 1f) };
        var world = NodeTransforms.World(nodes);
        Assert.Equal(3f, world[2].Translation.Y, precision: 4);
    }

    [Fact]
    public void AWellFormedTreeHasOneRootAndNoCycle()
    {
        Assert.True(NodeTransforms.IsWellFormed([Node(-1), Node(0), Node(1)]));

        // Two roots.
        Assert.False(NodeTransforms.IsWellFormed([Node(-1), Node(-1)]));
        // A link past the end.
        Assert.False(NodeTransforms.IsWellFormed([Node(-1), Node(9)]));
        // A cycle: each points at the other, so neither reaches a root.
        Assert.False(NodeTransforms.IsWellFormed([Node(1), Node(0)]));
    }

    [Fact]
    public void AnEmptyTreeIsVacuouslyFine()
    {
        Assert.True(NodeTransforms.IsWellFormed([]));
        Assert.Empty(NodeTransforms.World([]));
    }

    [Fact]
    public void AChildBeforeItsParentStillProducesAFiniteMatrix()
    {
        // The data always stores parents first, but nothing guarantees it, and a
        // forward pass must not read a matrix it has not written.
        var nodes = new[] { Node(1, ty: 5f), Node(-1, ty: 2f) };
        var world = NodeTransforms.World(nodes);
        Assert.All(world, m => Assert.True(float.IsFinite(m.Translation.Y)));
    }
}
