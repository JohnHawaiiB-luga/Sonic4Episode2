using System.Numerics;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Composes a motion onto a node tree and produces the world matrix per node.
/// </summary>
/// <remarks>
/// This is where the two halves meet. <see cref="NnNode"/> gives each node a base
/// transform, <see cref="MotionSampler"/> gives a channel's value at a frame, and
/// this overrides the base components a motion animates — leaving the rest at the
/// bind pose — then walks the hierarchy exactly as <see cref="NodeTransforms"/>
/// does for a still model.
/// <para>
/// It handles rigid models completely: a mesh set bound to a single node draws
/// with that node's world matrix, which covers every gimmick whose parts each
/// ride one node — jet walls, springs, propellers. A skinned model needs the
/// matrix palette on top of this, which is not decoded.
/// </para>
/// </remarks>
public static class AnimatedPose
{
    /// <summary>
    /// The world matrices for a node tree at a frame, with the motion applied.
    /// </summary>
    /// <param name="nodes">The model's nodes, in file order.</param>
    /// <param name="channels">Decoded channels of the motion to play.</param>
    /// <param name="frame">Frame to evaluate at.</param>
    public static Matrix4x4[] World(IReadOnlyList<NnNode> nodes,
                                    IReadOnlyList<MotionSampler> channels,
                                    float frame)
    {
        // Start from each node's bind-pose local components.
        var t = new Vector3[nodes.Count];
        var r = new Vector3[nodes.Count];
        var s = new Vector3[nodes.Count];
        for (int i = 0; i < nodes.Count; i++)
        {
            var n = nodes[i];
            t[i] = new Vector3(n.TranslateX, n.TranslateY, n.TranslateZ);
            var (rx, ry, rz) = n.RotationRadians;
            r[i] = new Vector3(rx, ry, rz);
            s[i] = new Vector3(n.ScaleX, n.ScaleY, n.ScaleZ);
        }

        // Override the components a channel animates.
        foreach (var channel in channels)
        {
            if ((uint)channel.Target >= (uint)nodes.Count) continue;
            float value = channel.Sample(frame);
            switch (channel.Component)
            {
                case MotionComponent.TranslateX: t[channel.Target].X = value; break;
                case MotionComponent.TranslateY: t[channel.Target].Y = value; break;
                case MotionComponent.TranslateZ: t[channel.Target].Z = value; break;
                case MotionComponent.RotateX: r[channel.Target].X = value; break;
                case MotionComponent.RotateY: r[channel.Target].Y = value; break;
                case MotionComponent.RotateZ: r[channel.Target].Z = value; break;
                case MotionComponent.ScaleX: s[channel.Target].X = value; break;
                case MotionComponent.ScaleY: s[channel.Target].Y = value; break;
                case MotionComponent.ScaleZ: s[channel.Target].Z = value; break;
            }
        }

        // Compose and walk the tree, same order as the static case.
        var world = new Matrix4x4[nodes.Count];
        var resolved = new bool[nodes.Count];
        for (int i = 0; i < nodes.Count; i++)
        {
            var local = Matrix4x4.CreateScale(s[i])
                      * Matrix4x4.CreateRotationZ(r[i].Z)
                      * Matrix4x4.CreateRotationY(r[i].Y)
                      * Matrix4x4.CreateRotationX(r[i].X)
                      * Matrix4x4.CreateTranslation(t[i]);
            int parent = nodes[i].Parent;
            world[i] = parent >= 0 && parent < nodes.Count && resolved[parent]
                ? local * world[parent]
                : local;
            resolved[i] = true;
        }
        return world;
    }

}
