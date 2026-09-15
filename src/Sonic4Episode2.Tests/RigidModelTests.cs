using System.Buffers.Binary;
using System.Text;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class RigidModelTests
{
    [Fact]
    public void BindPalettePreservesAuthoredSpaceAcrossMultipleNodes()
    {
        var model = Model();
        var mesh = TileMesh.ForRigidModel(model);
        Assert.Equal(new float[] { 13, 17, -19, 14, 17, -19, 13, 18, -19,
                                   18, 24, -8, 19, 24, -8, 18, 25, -8 }, mesh.Positions);
        Assert.Equal(new float[] { 0, 0, 1, 0, 0, 1, 0, 0, 1,
                                   0, 0, 1, 0, 0, 1, 0, 0, 1 }, mesh.Normals);
        Assert.Equal(new int[] { 0, 1, 2, 3, 4, 5 }, mesh.Indices);
        Assert.Throws<InvalidDataException>(() => TileMesh.ForNativeStage(model));
    }

    [Theory]
    [InlineData("matrix")]
    [InlineData("node")]
    [InlineData("duplicate")]
    [InlineData("parent")]
    [InlineData("rotation-order")]
    public void UnsupportedRigidBindingsFailExplicitly(string alteration)
    {
        Assert.Throws<InvalidDataException>(() => TileMesh.ForRigidModel(Model(alteration)));
    }

    private static NnModel Model(string? alteration = null)
    {
        var bytes = new byte[0xA00];
        void I(int at, int value) => BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(at), value);
        void S(int at, short value) => BinaryPrimitives.WriteInt16LittleEndian(bytes.AsSpan(at), value);
        void F(int at, float value) => I(at, BitConverter.SingleToInt32Bits(value));
        void Tag(int at, string tag) => Encoding.ASCII.GetBytes(tag).CopyTo(bytes, at);
        int At(int offset) => NnFile.DataBase + offset;
        Tag(0, "NZIF"); Tag(8, "NZOB"); I(12, 8); I(16, 0x100); Tag(24, "NEND");
        int h = At(0x100);
        F(h, 300); F(h+4, 500); F(h+8, -700);
        I(h+0x18, 1); I(h+0x1C, 0x200); I(h+0x20, 1); I(h+0x24, 0x210);
        I(h+0x28, 3); I(h+0x2C, 2); I(h+0x30, 0x400);
        I(h+0x34, 2); I(h+0x38, 1); I(h+0x3C, 0x700);
        I(At(0x204), 0x240); I(At(0x214), 0x280);
        int v = At(0x240);
        I(v, (int)VertexFormat.Position); I(v+8, 12); I(v+12, 3); I(v+16, 0x300);
        for (int i = 0; i < 3; i++)
        {
            F(At(0x300)+i*12, i == 1 ? 14 : 13);
            F(At(0x304)+i*12, i == 2 ? 18 : 17);
            F(At(0x308)+i*12, -19);
        }
        int p = At(0x280);
        I(p, (int)NnPrimitiveList.TriangleStrip); I(p+4, 3); I(p+8, 1);
        I(p+12, 0x340); I(p+16, 0x350); I(At(0x340), 3);
        S(At(0x350), 0); S(At(0x352), 1); S(At(0x354), 2);
        for (int i = 0; i < 3; i++)
        {
            int n = At(0x400)+i*NnNode.Size;
            I(n, i == 0 ? 15 : i == 1 ? 6 : 14);
            S(n+4, (short)(i-1)); S(n+6, (short)(i == 0 ? -1 : 0));
            S(n+8, (short)(i == 0 ? 1 : -1)); S(n+10, (short)(i == 1 ? 2 : -1));
            F(n+0xC, 5); F(n+0x10, 7); F(n+0x14, 11);
            F(n+0x30, 1); F(n+0x44, 1); F(n+0x58, 1); F(n+0x6C, 1);
            F(n+0x60, -5); F(n+0x64, -7); F(n+0x68, -11);
        }
        I(At(0x704), 2); I(At(0x708), 0x740);
        for (int i = 0; i < 2; i++)
        {
            int m = At(0x740)+i*NnMeshSet.Size;
            I(m+0x10, i+1); I(m+0x14, i); I(m+0x18, -1);
        }
        if (alteration == "matrix") I(At(0x754), 2);
        if (alteration == "node") I(At(0x750), 0);
        if (alteration == "duplicate") S(At(0x400)+2*NnNode.Size+4, 0);
        if (alteration == "parent") S(At(0x400)+NnNode.Size+6, 2);
        if (alteration == "rotation-order") I(At(0x400)+NnNode.Size, 0x100);
        return NnModel.Load(bytes)!;
    }
}
