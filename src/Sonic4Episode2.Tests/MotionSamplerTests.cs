using System.Buffers.Binary;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class MotionSamplerTests
{
    /// <summary>A channel whose 8-byte keys are the given (frame, value) pairs.</summary>
    private static (NnSubMotion, byte[]) FloatChannel(uint flags, params (float F, float V)[] keys)
    {
        // Keys sit at NnFile.DataBase; give the buffer a header-sized preamble.
        var buffer = new byte[NnFile.DataBase + keys.Length * 8];
        for (int i = 0; i < keys.Length; i++)
        {
            BitConverter.GetBytes(keys[i].F).CopyTo(buffer, NnFile.DataBase + i * 8);
            BitConverter.GetBytes(keys[i].V).CopyTo(buffer, NnFile.DataBase + i * 8 + 4);
        }
        var channel = new NnSubMotion(flags, 0, 3, 0, 0, 0, 0,
                                      keys.Length, 8, 0);
        return (channel, buffer);
    }

    /// <summary>A rotation channel: 4-byte keys of signed frame, signed A16 value.</summary>
    private static (NnSubMotion, byte[]) RotationChannel(params (short F, short V)[] keys)
    {
        var buffer = new byte[NnFile.DataBase + keys.Length * 4];
        for (int i = 0; i < keys.Length; i++)
        {
            BinaryPrimitives.WriteInt16LittleEndian(
                buffer.AsSpan(NnFile.DataBase + i * 4), keys[i].F);
            BinaryPrimitives.WriteInt16LittleEndian(
                buffer.AsSpan(NnFile.DataBase + i * 4 + 2), keys[i].V);
        }
        var channel = new NnSubMotion(0x0812, 0, 5, 0, 0, 0, 0,
                                      keys.Length, 4, 0);
        return (channel, buffer);
    }

    [Fact]
    public void FloatKeysDecodeAsFrameValuePairs()
    {
        var (channel, buffer) = FloatChannel(0x0201, (0f, 4.5f), (10f, 1.5f));
        var sampler = MotionSampler.Decode(channel, buffer)!;

        Assert.Equal(2, sampler.KeyCount);
        Assert.False(sampler.IsRotation);
        Assert.Equal(3, sampler.Target);
        Assert.Equal(4.5f, sampler.Sample(0f));
        Assert.Equal(1.5f, sampler.Sample(10f));
    }

    [Fact]
    public void ValuesInterpolateLinearlyBetweenKeys()
    {
        var (channel, buffer) = FloatChannel(0x0201, (0f, 0f), (10f, 20f));
        var sampler = MotionSampler.Decode(channel, buffer)!;
        Assert.Equal(10f, sampler.Sample(5f), precision: 4);
        Assert.Equal(4f, sampler.Sample(2f), precision: 4);
    }

    [Fact]
    public void OutsideTheRangeHoldsAtTheNearestKey()
    {
        var (channel, buffer) = FloatChannel(0x0201, (2f, 7f), (8f, 9f));
        var sampler = MotionSampler.Decode(channel, buffer)!;
        Assert.Equal(7f, sampler.Sample(-100f));
        Assert.Equal(9f, sampler.Sample(100f));
    }

    [Fact]
    public void RotationKeysReadTheFrameAsSigned()
    {
        // The pre-roll key at frame -5, the shape that made the first pass look
        // non-monotonic.
        var (channel, buffer) = RotationChannel((-5, 0), (25, 16384));
        var sampler = MotionSampler.Decode(channel, buffer)!;
        Assert.Equal(-5f, sampler.FrameOf(0));
        Assert.Equal(25f, sampler.FrameOf(1));
    }

    [Fact]
    public void RotationValuesComeBackInRadians()
    {
        // 16384 A16 units is a quarter turn.
        var (channel, buffer) = RotationChannel((0, 0), (10, 16384));
        var sampler = MotionSampler.Decode(channel, buffer)!;
        Assert.True(sampler.IsRotation);
        Assert.Equal(0f, sampler.Sample(0f), precision: 4);
        Assert.Equal(MathF.Tau / 4f, sampler.Sample(10f), precision: 4);
    }

    [Fact]
    public void AZeroKeyChannelSamplesToZeroRatherThanThrowing()
    {
        var channel = new NnSubMotion(0x0201, 0, 3, 0, 0, 0, 0, 0, 8, 0);
        var sampler = MotionSampler.Decode(channel, new byte[NnFile.DataBase])!;
        Assert.Equal(0f, sampler.Sample(5f));
    }

    [Fact]
    public void AKeyRangePastTheBufferIsRejected()
    {
        var channel = new NnSubMotion(0x0201, 0, 3, 0, 0, 0, 0, 100, 8, 0);
        Assert.Null(MotionSampler.Decode(channel, new byte[NnFile.DataBase + 8]));
    }
}
