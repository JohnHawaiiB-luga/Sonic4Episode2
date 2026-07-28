using System.Numerics;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Builds the skinning matrix palette from a posed node tree.
/// </summary>
/// <remarks>
/// The algorithm is <c>nnCalcMatrixPaletteNode</c>'s, read from the symbolized
/// Android build and verified against this build's data: every node whose
/// palette index (<see cref="NnNode.MatrixIndex"/>) is not -1 writes one slot,
/// <c>palette[index] = InverseBind · world</c> in row-vector order — the vertex
/// is carried from bind space into bone space and back out through the current
/// pose. A node flagged <see cref="NnNode.UnitInitMatrix"/> copies its world
/// matrix untouched; its stored inverse-bind bytes are stale and unread.
/// <para>
/// A vertex does not index this palette directly: its <c>UBYTE4</c> blend index
/// selects into the vertex list's <see cref="NnVertexList.MatrixIndices"/>,
/// which holds the palette slot. <see cref="TileMesh"/> applies that chain.
/// </para>
/// </remarks>
public static class MatrixPalette
{
    /// <summary>
    /// One matrix per palette slot for a pose.
    /// </summary>
    /// <param name="nodes">The model's nodes, in file order.</param>
    /// <param name="world">
    /// World matrix per node — <see cref="NodeTransforms.World"/> for the bind
    /// pose or <see cref="AnimatedPose.World"/> for a motion frame.
    /// </param>
    /// <param name="count">
    /// Slot count, <see cref="NnObject.MatrixPaletteCount"/>. Slots no node
    /// claims stay identity.
    /// </param>
    public static Matrix4x4[] Build(IReadOnlyList<NnNode> nodes,
                                    IReadOnlyList<Matrix4x4> world,
                                    int count)
    {
        var palette = new Matrix4x4[count];
        for (int i = 0; i < count; i++) palette[i] = Matrix4x4.Identity;

        int limit = Math.Min(nodes.Count, world.Count);
        for (int i = 0; i < limit; i++)
        {
            int slot = nodes[i].MatrixIndex;
            if (slot < 0 || slot >= count) continue;
            palette[slot] = nodes[i].HasUnitInverseBind
                ? world[i]
                : nodes[i].InverseBind * world[i];
        }
        return palette;
    }
}
