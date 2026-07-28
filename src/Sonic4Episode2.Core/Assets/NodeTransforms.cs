using System.Numerics;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Resolves a model's node tree into one world matrix per node.
/// </summary>
/// <remarks>
/// A skinned model's vertices are authored against this pose. Nothing can be
/// drawn from a multi-node model without it, and every motion in
/// <c>*_MTN.AMB</c> is expressed as a change to it, so this is the piece both
/// static rendering and animation stand on.
/// <para>
/// Local transform is scale, then rotation, then translation, with rotation
/// applied <b>X then Y then Z</b>. The order field (<c>flags &amp; 0xF00</c>) is
/// zero — XYZ — on every node in the build, and the data settles the order
/// beyond doubt: composing <c>SON_SPINMODEL</c>'s bind chain XYZ and multiplying
/// by each node's stored inverse bind gives identity to 3e-6, while every other
/// order fails by 1.0 or worse. An earlier version of this file rotated ZYX and
/// looked right, because rigid gimmicks rotate about a single axis.
/// </para>
/// <para>
/// Flag bits 0-2 declare the stored translation / rotation / scale identity, and
/// the engine skips the component without reading it — <c>SON_MODEL</c> carries
/// non-identity bytes under set flags, so honoring them is not optional.
/// </para>
/// </remarks>
public static class NodeTransforms
{
    /// <summary>The local transform of a single node.</summary>
    public static Matrix4x4 Local(NnNode node)
    {
        var m = Matrix4x4.Identity;
        if ((node.Flags & NnNode.UnitTranslation) == 0)
            m = Matrix4x4.CreateTranslation(node.TranslateX, node.TranslateY, node.TranslateZ);
        if ((node.Flags & NnNode.UnitRotation) == 0)
        {
            var (rx, ry, rz) = node.RotationRadians;
            m = Matrix4x4.CreateRotationX(rx)
              * Matrix4x4.CreateRotationY(ry)
              * Matrix4x4.CreateRotationZ(rz)
              * m;
        }
        if ((node.Flags & NnNode.UnitScaling) == 0)
            m = Matrix4x4.CreateScale(node.ScaleX, node.ScaleY, node.ScaleZ) * m;
        return m;
    }

    /// <summary>
    /// World matrices for every node, in node order.
    /// </summary>
    /// <remarks>
    /// Nodes are stored parents-before-children, so a single forward pass
    /// suffices — but that ordering is an observation about this data rather than
    /// a guarantee, so a node whose parent has not been resolved yet falls back to
    /// its local transform instead of reading a matrix that is not there.
    /// </remarks>
    public static Matrix4x4[] World(IReadOnlyList<NnNode> nodes)
    {
        var world = new Matrix4x4[nodes.Count];
        var resolved = new bool[nodes.Count];

        for (int i = 0; i < nodes.Count; i++)
        {
            var local = Local(nodes[i]);
            int parent = nodes[i].Parent;
            world[i] = parent >= 0 && parent < nodes.Count && resolved[parent]
                ? local * world[parent]
                : local;
            resolved[i] = true;
        }
        return world;
    }

    /// <summary>
    /// Whether a node tree is well formed: one root, every link in range, and no
    /// cycle reachable by walking parents.
    /// </summary>
    public static bool IsWellFormed(IReadOnlyList<NnNode> nodes)
    {
        if (nodes.Count == 0) return true;
        if (nodes.Count(n => n.IsRoot) != 1) return false;

        for (int i = 0; i < nodes.Count; i++)
        {
            foreach (int link in new[] { nodes[i].Parent, nodes[i].Child, nodes[i].Sibling })
                if (link < -1 || link >= nodes.Count) return false;

            // Walking parents must reach the root in at most Count steps.
            int at = nodes[i].Parent, steps = 0;
            while (at >= 0)
            {
                if (++steps > nodes.Count) return false;
                at = nodes[at].Parent;
            }
        }
        return true;
    }
}
