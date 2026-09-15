using System.Buffers.Binary;
using System.Text;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class VertexColourTests
{
    private static readonly byte[] RawBgra =
    [
        0, 91, 199, 255,
        3, 2, 1, 4,
        255, 0, 17, 231,
    ];

    private static readonly byte[] ExpectedRgba =
    [
        199, 91, 0, 255,
        1, 2, 3, 4,
        17, 0, 255, 231,
    ];

    [Fact]
    public void DiffuseColoursConvertD3dBgraToRgbaWithoutLoss()
    {
        var list = VertexList(VertexFormat.Position | VertexFormat.Diffuse, 16, 2,
                              VertexBytes(RawBgra[..8]));
        var rgba = new byte[8];

        Assert.True(list.ReadDiffuseColors(rgba));
        Assert.Equal(ExpectedRgba[..8], rgba);
    }

    [Fact]
    public void MissingDiffuseColoursDefaultToOpaqueWhite()
    {
        var list = VertexList(VertexFormat.Position, 12, 2, new byte[24]);
        var rgba = new byte[8];

        Assert.False(list.ReadDiffuseColors(rgba));
        Assert.Equal(new byte[] { 255, 255, 255, 255, 255, 255, 255, 255 }, rgba);

        var mesh = TileMesh.From(Model(withDiffuse: false));
        Assert.Equal(new byte[]
        {
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        }, mesh.Colors);

        var optional = new TileMesh
        {
            Positions = [0, 0, 0, 1, 0, 0, 0, 1, 0],
            TexCoords = [0, 0, 1, 0, 0, 1],
            Normals = [0, 0, 1, 0, 0, 1, 0, 0, 1],
            Indices = [0, 1, 2],
            TriangleMaterials = [MaterialKey.FromBase(null)],
        };
        Assert.Empty(optional.Colors);
        var batch = new StageBatch();
        batch.Add(optional, 0, 0, 0);
        Assert.Equal(new byte[]
        {
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        }, batch.Colors);
    }

    [Fact]
    public void DiffuseColourReadsRejectShortDestinationsAndTruncatedStreams()
    {
        var valid = VertexList(VertexFormat.Position | VertexFormat.Diffuse, 16, 1,
                               VertexBytes(RawBgra[..4]));
        Assert.Throws<ArgumentException>(() => valid.ReadDiffuseColors(new byte[3]));

        var truncated = VertexList(VertexFormat.Position | VertexFormat.Diffuse, 16, 2,
                                   VertexBytes(RawBgra[..4]));
        Assert.Throws<NnException>(() => truncated.ReadDiffuseColors(new byte[8]));

        var malformedStride = VertexList(VertexFormat.Position | VertexFormat.Diffuse, 15, 1,
                                         new byte[15]);
        Assert.Throws<NnException>(() => malformedStride.ReadDiffuseColors(new byte[4]));
    }

    [Fact]
    public void MeshesAndBatchesPreserveEveryVertexColour()
    {
        var model = Model(withDiffuse: true);
        var from = TileMesh.From(model);
        var native = TileMesh.ForNativeStage(model);

        Assert.Equal(ExpectedRgba, from.Colors);
        Assert.Equal(ExpectedRgba, native.Colors);

        var batch = new StageBatch();
        batch.Add(native, 13, -7, 2);
        Assert.Equal(ExpectedRgba, batch.Colors);
    }

    private static NnVertexList VertexList(VertexFormat format, int stride, int count,
                                           ReadOnlySpan<byte> vertexBytes)
    {
        const int bufferOffset = 0x40;
        var data = new byte[At(bufferOffset) + vertexBytes.Length];
        WriteU32(data, 0, (uint)format);
        WriteI32(data, 8, stride);
        WriteI32(data, 12, count);
        WriteI32(data, 16, bufferOffset);
        vertexBytes.CopyTo(data.AsSpan(At(bufferOffset)));
        return NnVertexList.Parse(data, 0);
    }

    private static byte[] VertexBytes(ReadOnlySpan<byte> colours)
    {
        int count = colours.Length / 4;
        var bytes = new byte[count * 16];
        for (int i = 0; i < count; i++)
            colours.Slice(i * 4, 4).CopyTo(bytes.AsSpan(i * 16 + 12, 4));
        return bytes;
    }

    private static NnModel Model(bool withDiffuse)
    {
        const int objectOffset = 0x100;
        const int vertexPointerOffset = 0x200;
        const int vertexDescriptorOffset = 0x220;
        const int vertexDataOffset = 0x260;
        const int primitivePointerOffset = 0x2C0;
        const int primitiveDescriptorOffset = 0x2E0;
        const int stripCountsOffset = 0x320;
        const int indicesOffset = 0x330;
        const int nodeOffset = 0x360;
        const int subObjectOffset = 0x400;
        const int meshSetOffset = 0x430;

        int stride = withDiffuse ? 16 : 12;
        var data = new byte[At(0x500)];
        WriteTag(data, 0, "NZIF");
        WriteTag(data, 8, "NZOB");
        WriteI32(data, 12, 8);
        WriteI32(data, 16, objectOffset);
        WriteTag(data, 24, "NEND");

        int header = At(objectOffset);
        WriteI32(data, header + 0x18, 1);
        WriteI32(data, header + 0x1C, vertexPointerOffset);
        WriteI32(data, header + 0x20, 1);
        WriteI32(data, header + 0x24, primitivePointerOffset);
        WriteI32(data, header + 0x28, 1);
        WriteI32(data, header + 0x2C, 1);
        WriteI32(data, header + 0x30, nodeOffset);
        WriteI32(data, header + 0x34, 1);
        WriteI32(data, header + 0x38, 1);
        WriteI32(data, header + 0x3C, subObjectOffset);

        int vertexPointer = At(vertexPointerOffset);
        WriteI32(data, vertexPointer + 4, vertexDescriptorOffset);
        int descriptor = At(vertexDescriptorOffset);
        WriteU32(data, descriptor, (uint)(VertexFormat.Position |
                                           (withDiffuse ? VertexFormat.Diffuse : (VertexFormat)0)));
        WriteI32(data, descriptor + 8, stride);
        WriteI32(data, descriptor + 12, 3);
        WriteI32(data, descriptor + 16, vertexDataOffset);
        for (int i = 0; i < 3; i++)
        {
            int vertex = At(vertexDataOffset + i * stride);
            WriteF32(data, vertex, i == 1 ? 1 : 0);
            WriteF32(data, vertex + 4, i == 2 ? 1 : 0);
            if (withDiffuse) RawBgra.AsSpan(i * 4, 4).CopyTo(data.AsSpan(vertex + 12, 4));
        }

        int primitivePointer = At(primitivePointerOffset);
        WriteI32(data, primitivePointer + 4, primitiveDescriptorOffset);
        int primitive = At(primitiveDescriptorOffset);
        WriteU32(data, primitive, NnPrimitiveList.TriangleStrip);
        WriteI32(data, primitive + 4, 3);
        WriteI32(data, primitive + 8, 1);
        WriteI32(data, primitive + 12, stripCountsOffset);
        WriteI32(data, primitive + 16, indicesOffset);
        WriteI32(data, At(stripCountsOffset), 3);
        WriteU16(data, At(indicesOffset), 0);
        WriteU16(data, At(indicesOffset + 2), 1);
        WriteU16(data, At(indicesOffset + 4), 2);

        int node = At(nodeOffset);
        WriteU32(data, node, NnNode.UnitTranslation | NnNode.UnitRotation |
                                NnNode.UnitScaling | NnNode.UnitInitMatrix);
        WriteI16(data, node + 4, 0);
        WriteI16(data, node + 6, -1);
        WriteI16(data, node + 8, -1);
        WriteI16(data, node + 10, -1);

        int subObject = At(subObjectOffset);
        WriteI32(data, subObject + 4, 1);
        WriteI32(data, subObject + 8, meshSetOffset);
        int mesh = At(meshSetOffset);
        WriteI32(data, mesh + 0x10, 0);
        WriteI32(data, mesh + 0x14, 0);
        WriteI32(data, mesh + 0x18, -1);

        return NnModel.Load(data) ?? throw new InvalidOperationException("vertex colour fixture did not load");
    }

    private static int At(int relativeOffset) => NnFile.DataBase + relativeOffset;

    private static void WriteTag(byte[] data, int at, string tag) =>
        Encoding.ASCII.GetBytes(tag).CopyTo(data, at);

    private static void WriteI16(byte[] data, int at, short value) =>
        BinaryPrimitives.WriteInt16LittleEndian(data.AsSpan(at), value);

    private static void WriteU16(byte[] data, int at, ushort value) =>
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(at), value);

    private static void WriteI32(byte[] data, int at, int value) =>
        BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(at), value);

    private static void WriteU32(byte[] data, int at, uint value) =>
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(at), value);

    private static void WriteF32(byte[] data, int at, float value) =>
        WriteI32(data, at, BitConverter.SingleToInt32Bits(value));
}
