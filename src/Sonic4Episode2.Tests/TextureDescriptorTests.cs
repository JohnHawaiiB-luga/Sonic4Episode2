using System.Buffers.Binary;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class TextureDescriptorTests
{
    private const uint StandardShaderDescriptor = 0x10000000;
    private const int MaterialOffset = 0x20;
    private const int StageOffset = 0x80;

    [Fact]
    public void StandardDescriptorsUse64ByteStrideAndDoNotReadSamplerTailsAsStages()
    {
        uint[] flags = [0x60000001, 0x60000002, 0x60000004, 0x60000008, 0x60000800];
        int[] indices = [3, 0, 2, 1, 1];
        var data = DescriptorData(flags.Length, StageOffset, flags.Length * 64);
        int start = At(StageOffset);
        for (int i = 0; i < flags.Length; i++)
        {
            int descriptor = start + i * 64;
            WriteU32(data, descriptor, flags[i]);
            WriteI32(data, descriptor + 4, indices[i]);
            WriteU32(data, descriptor + 0x20, 0x00010001u + (uint)i);
            WriteI32(data, descriptor + 0x24, 99 - i);
        }

        var material = Parse(data, StandardShaderDescriptor);

        Assert.Equal(flags, material.Stages.Select(stage => stage.Flags).ToArray());
        Assert.Equal(indices, material.Stages.Select(stage => stage.Index).ToArray());
        Assert.Equal(5, material.LiveStages.Count());
        Assert.DoesNotContain(0x00010001u, material.Stages.Select(stage => stage.Flags));
        Assert.True(material.Stages[4].IsLive);
        Assert.Equal(TextureRole.Unknown, material.Stages[4].Role);
        Assert.Equal(indices[1], material.TextureIndex);
    }

    [Fact]
    public void ParsedHighFlagBindingPreservesPublicValueEquality()
    {
        var data = DescriptorData(count: 1, stageOffset: StageOffset, stageBytes: 64);
        WriteU32(data, At(StageOffset), 0x60000002);
        WriteI32(data, At(StageOffset) + 4, 4);
        var parsed = Assert.Single(Parse(data, StandardShaderDescriptor).Stages);
        var constructed = new NnTextureStage(0x60000002, 4);

        Assert.Equal(constructed, parsed);
        Assert.True(constructed == parsed);
        Assert.False(constructed != parsed);
        Assert.Equal(constructed.GetHashCode(), parsed.GetHashCode());
        var lowFlags = parsed with { Flags = 2 };
        Assert.True(lowFlags.IsLive);
        Assert.NotEqual(new NnTextureStage(2, 4), lowFlags);
    }

    [Fact]
    public void RecognizedDescriptorsRejectTruncatedFullRecords()
    {
        var data = DescriptorData(count: 2, stageOffset: StageOffset, stageBytes: 96);
        WriteU32(data, At(StageOffset), 0x60000002);

        Assert.Throws<NnException>(() => Parse(data, StandardShaderDescriptor));
    }

    [Fact]
    public void StandardDescriptorsKeepLowOnlyRoleFlagsSemanticallyLive()
    {
        var data = DescriptorData(count: 1, stageOffset: StageOffset, stageBytes: 64);
        WriteU32(data, At(StageOffset), 0x00000002);
        WriteI32(data, At(StageOffset) + 4, 4);

        var material = Parse(data, StandardShaderDescriptor);
        var stage = Assert.Single(material.Stages);

        Assert.True(stage.IsLive);
        Assert.Equal(TextureRole.Base, stage.Role);
        Assert.Equal(4, material.TextureIndex);
        Assert.False(new NnTextureStage(0x00000002, 4).IsLive);
    }

    [Fact]
    public void RecognizedDescriptorsRejectInvalidCountsAndOffsets()
    {
        Assert.Throws<NnException>(() => Parse(
            DescriptorData(count: -1, stageOffset: StageOffset, stageBytes: 0), StandardShaderDescriptor));
        Assert.Throws<NnException>(() => Parse(
            DescriptorData(count: 17, stageOffset: StageOffset, stageBytes: 8), StandardShaderDescriptor));
        Assert.Throws<NnException>(() => Parse(
            DescriptorData(count: int.MaxValue, stageOffset: StageOffset, stageBytes: 0), StandardShaderDescriptor));
        Assert.Throws<NnException>(() => Parse(
            DescriptorData(count: 1, stageOffset: int.MaxValue, stageBytes: 0), StandardShaderDescriptor));
    }

    [Fact]
    public void MissingOptionalStageBlockLeavesRecognizedMaterialUntextured()
    {
        var data = DescriptorData(count: 1, stageOffset: StageOffset, stageBytes: 64);
        WriteU32(data, At(StageOffset), 0x60000002);
        WriteI32(data, At(StageOffset) + 4, 4);

        var material = Parse(data, StandardShaderDescriptor, hasStagePointer: false);

        Assert.Empty(material.Stages);
        Assert.Null(material.TextureIndex);
    }

    [Fact]
    public void UnknownDescriptorsRetainLegacy32ByteCompatibility()
    {
        var data = DescriptorData(count: 2, stageOffset: StageOffset, stageBytes: 64);
        WriteU32(data, At(StageOffset), 0x60000002);
        WriteI32(data, At(StageOffset) + 4, 4);
        WriteU32(data, At(StageOffset) + 32, 0x60000004);
        WriteI32(data, At(StageOffset) + 36, 7);

        var material = Parse(data, 0x20000000);

        Assert.Equal(2, material.Stages.Count);
        Assert.Equal(4, material.TextureIndex);
        Assert.Equal(7, material.IndexFor(TextureRole.Environment));
    }

    private static NnMaterial Parse(byte[] data, uint pointerFlags, bool hasStagePointer = true)
    {
        var relocations = new HashSet<int>();
        if (hasStagePointer) relocations.Add(MaterialOffset + 0x18);
        return NnMaterial.Parse(data, MaterialOffset, pointerFlags, relocations);
    }

    private static byte[] DescriptorData(int count, int stageOffset, int stageBytes)
    {
        int materialEnd = At(MaterialOffset) + 0x1C;
        int stageEnd = stageOffset >= 0 && stageOffset <= int.MaxValue - NnFile.DataBase
            ? At(stageOffset) + Math.Max(stageBytes, 0)
            : 0;
        var data = new byte[Math.Max(materialEnd, stageEnd)];
        int material = At(MaterialOffset);
        WriteI32(data, material + 0x14, count);
        WriteI32(data, material + 0x18, stageOffset);
        return data;
    }

    private static int At(int relativeOffset) => NnFile.DataBase + relativeOffset;

    private static void WriteI32(byte[] data, int at, int value) =>
        BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(at), value);

    private static void WriteU32(byte[] data, int at, uint value) =>
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(at), value);
}
