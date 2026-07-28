using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class AnimatedPoseTests
{
    private static NnNode Node(short parent, float tx = 0, float ty = 0, float tz = 0) =>
        new(0, 0, parent, -1, -1, tx, ty, tz, 0, 0, 0, 1, 1, 1);

    /// <summary>A float channel: type flags, target node, and (frame, value) keys.</summary>
    private static MotionSampler FloatChannel(uint type, int target, params (float F, float V)[] keys)
    {
        var buffer = new byte[NnFile.DataBase + keys.Length * 8];
        for (int i = 0; i < keys.Length; i++)
        {
            BitConverter.GetBytes(keys[i].F).CopyTo(buffer, NnFile.DataBase + i * 8);
            BitConverter.GetBytes(keys[i].V).CopyTo(buffer, NnFile.DataBase + i * 8 + 4);
        }
        var header = new NnSubMotion(type, 0, target, 0, 0, 0, 0, keys.Length, 8, 0);
        return MotionSampler.Decode(header, buffer)!;
    }

    [Fact]
    public void ComponentsAreReadFromTheTypeBits()
    {
        Assert.Equal(MotionComponent.TranslateY, FloatChannel(0x0201, 0, (0, 0)).Component);
        Assert.Equal(MotionComponent.ScaleX, FloatChannel(0x8001, 0, (0, 0)).Component);
        Assert.Equal(MotionComponent.RotateZ, FloatChannel(0x2012, 0, (0, 0)).Component);
    }

    [Fact]
    public void WithNoChannelsThePoseIsTheBindPose()
    {
        var nodes = new[] { Node(-1, ty: 10f), Node(0, ty: 5f) };
        var world = AnimatedPose.World(nodes, [], frame: 0f);
        Assert.Equal(10f, world[0].Translation.Y, precision: 4);
        Assert.Equal(15f, world[1].Translation.Y, precision: 4);
    }

    [Fact]
    public void ATranslationChannelOverridesTheNodesPosition()
    {
        var nodes = new[] { Node(-1, ty: 10f) };
        // Animate node 0's Y from 10 to 30 over ten frames.
        var channel = FloatChannel(0x0201, 0, (0f, 10f), (10f, 30f));

        Assert.Equal(10f, AnimatedPose.World(nodes, [channel], 0f)[0].Translation.Y, 4);
        Assert.Equal(20f, AnimatedPose.World(nodes, [channel], 5f)[0].Translation.Y, 4);
        Assert.Equal(30f, AnimatedPose.World(nodes, [channel], 10f)[0].Translation.Y, 4);
    }

    [Fact]
    public void AnAnimatedParentCarriesItsChild()
    {
        var nodes = new[] { Node(-1), Node(0, ty: 5f) };
        var channel = FloatChannel(0x0201, 0, (0f, 0f), (10f, 100f));

        // The child sits 5 above the parent, so it tracks the parent plus 5.
        Assert.Equal(105f, AnimatedPose.World(nodes, [channel], 10f)[1].Translation.Y, 4);
    }

    [Fact]
    public void AChannelTargetingAMissingNodeIsIgnored()
    {
        var nodes = new[] { Node(-1) };
        var channel = FloatChannel(0x0201, target: 9, (0f, 50f));
        // Must not throw, and node 0 keeps its bind pose.
        Assert.Equal(0f, AnimatedPose.World(nodes, [channel], 0f)[0].Translation.Y, 4);
    }

    [Fact]
    public void AScaleChannelScalesAChildsOffset()
    {
        var nodes = new[] { Node(-1), Node(0, tx: 3f) };
        var channel = FloatChannel(0x8001, 0, (0f, 1f), (10f, 2f));  // scale X 1 -> 2
        Assert.Equal(3f, AnimatedPose.World(nodes, [channel], 0f)[1].Translation.X, 4);
        Assert.Equal(6f, AnimatedPose.World(nodes, [channel], 10f)[1].Translation.X, 4);
    }
}
