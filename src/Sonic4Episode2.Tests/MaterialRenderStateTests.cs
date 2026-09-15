using System.Buffers.Binary;
using System.Text;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class MaterialRenderStateTests
{
    private const uint StandardShaderDescriptor = 0x10000000;

    [Fact]
    public void MissingAndUnrecognizedStateDescriptorsKeepExistingDefaults()
    {
        var missing = Parse(StandardShaderDescriptor, hasState: false);
        var unknown = Parse(0x20000000, materialFlags: 0x1E02u,
                            logic: 0x18u, alphaComparison: 5, depthComparison: 7,
                            alphaReference: 247);

        Assert.Equal(MaterialRenderState.Default, missing.RenderState);
        Assert.Equal(MaterialRenderState.Default, unknown.RenderState);
        Assert.Equal(MaterialBlend.Additive, unknown.Blend);
    }

    [Fact]
    public void StandardStateMapsMaterialFlagsAndLogicBits()
    {
        var material = Parse(StandardShaderDescriptor,
                             materialFlags: 0x0B02u,
                             logic: 0x18u,
                             alphaComparison: 5,
                             depthComparison: 7,
                             alphaReference: 247);

        Assert.Equal(new MaterialRenderState(
            LightingEnabled: false,
            BlendEnabled: false,
            DepthTestEnabled: true,
            DepthWriteEnabled: false,
            ColorWriteMask: 0x0A,
            AlphaComparison: 5,
            AlphaReference: 247,
            DepthComparison: 7), material.RenderState);
        Assert.Equal(MaterialBlend.Additive, material.Blend);
    }

    [Fact]
    public void ParsedStateFlowsIntoTileTriangleMaterial()
    {
        var mesh = TileMesh.From(ModelWithState(
            materialFlags: 0x0B02u,
            logic: 0x18u,
            alphaComparison: 5,
            depthComparison: 7,
            alphaReference: 247));

        var key = Assert.Single(mesh.TriangleMaterials);
        Assert.Equal(new MaterialRenderState(false, false, true, false, 0x0A, 5, 247, 7),
                     key.RenderState);
        Assert.Equal(MaterialBlend.Additive, key.Blend);
    }

    [Theory]
    [InlineData(0x0000u, 0x0Fu)]
    [InlineData(0x0200u, 0x0Eu)]
    [InlineData(0x0400u, 0x0Du)]
    [InlineData(0x0800u, 0x0Bu)]
    [InlineData(0x1000u, 0x07u)]
    [InlineData(0x1E00u, 0x00u)]
    public void MaterialColorWriteBitsProduceTheSemanticRgbaMask(uint flags, uint expectedMask)
    {
        var material = Parse(StandardShaderDescriptor, materialFlags: flags);

        Assert.Equal((byte)expectedMask, material.RenderState.ColorWriteMask);
    }

    [Fact]
    public void DisabledAlphaAndDepthTestsCanonicalizeTheirInactiveFields()
    {
        var material = Parse(StandardShaderDescriptor,
                             logic: 0x01u,
                             alphaComparison: 5,
                             depthComparison: 7,
                             alphaReference: 247);

        Assert.True(material.RenderState.BlendEnabled);
        Assert.False(material.RenderState.DepthTestEnabled);
        Assert.Equal(8, material.RenderState.AlphaComparison);
        Assert.Equal(0, material.RenderState.AlphaReference);
        Assert.Equal(4, material.RenderState.DepthComparison);
    }

    [Fact]
    public void ActiveComparisonsOutsideTheD3dRangeAreRejected()
    {
        Assert.Throws<NnException>(() => Parse(StandardShaderDescriptor,
                                                logic: 0x08u,
                                                alphaComparison: 0));
        Assert.Throws<NnException>(() => Parse(StandardShaderDescriptor,
                                                logic: 0x10u,
                                                depthComparison: 9));
    }

    [Fact]
    public void TruncatedStandardStateFallsBackWithoutChangingExistingBlendDecoding()
    {
        var material = Parse(StandardShaderDescriptor,
                             materialFlags: 0x1E02u,
                             logic: 0x18u,
                             alphaComparison: 5,
                             depthComparison: 7,
                             alphaReference: 247,
                             stateLength: 27);

        Assert.Equal(MaterialRenderState.Default, material.RenderState);
        Assert.Equal(MaterialBlend.Additive, material.Blend);
    }

    [Fact]
    public void StandardDescriptorReadsColourTermsWhenColourFlagsAreZero()
    {
        var model = ModelWithState(
            materialFlags: 0,
            logic: 0x01u,
            alphaComparison: 8,
            depthComparison: 4,
            alphaReference: 0,
            colourFlags: 0,
            ambient: (0.125f, 0.25f, 0.5f, 0.75f),
            diffuse: (0.8f, 0.6f, 0.4f, 0.2f));

        var material = Assert.Single(model.Materials);
        Assert.Equal((0.125f, 0.25f, 0.5f, 0.75f), material.Ambient);
        Assert.Equal((0.8f, 0.6f, 0.4f, 0.2f), material.Diffuse);
    }

    [Fact]
    public void MaterialKeysKeepRenderStateDistinctWhileTheThreeArgumentApiDefaults()
    {
        var textures = new MaterialTextures("MASK.DDS", null, null, null);
        var ordinary = new MaterialKey(
            Textures: textures,
            Blend: MaterialBlend.Alpha,
            Diffuse: MaterialKey.White);
        var (deconstructedTextures, deconstructedBlend, deconstructedDiffuse) = ordinary;
        var depthOnly = new MaterialKey(
            Textures: textures,
            Blend: MaterialBlend.Alpha,
            Diffuse: MaterialKey.White)
        {
            RenderState = MaterialRenderState.Default with
            {
                LightingEnabled = false,
                BlendEnabled = false,
                DepthWriteEnabled = false,
                ColorWriteMask = 0,
                AlphaComparison = 5,
                AlphaReference = 247,
                DepthComparison = 5,
            },
        };

        Assert.Equal(MaterialRenderState.Default, ordinary.RenderState);
        Assert.Equal(textures, deconstructedTextures);
        Assert.Equal(MaterialBlend.Alpha, deconstructedBlend);
        Assert.Equal(MaterialKey.White, deconstructedDiffuse);
        Assert.NotEqual(ordinary, depthOnly);

        var batches = new Dictionary<MaterialKey, int>
        {
            [ordinary] = 1,
            [depthOnly] = 1,
        };
        Assert.Equal(2, batches.Count);
    }

    private static NnMaterial Parse(
        uint pointerFlags,
        uint materialFlags = 0,
        uint logic = 0x01u,
        ushort alphaComparison = 8,
        ushort depthComparison = 4,
        uint alphaReference = 0,
        int stateLength = 28,
        bool hasState = true)
    {
        const int materialOffset = 0x20;
        const int stateOffset = 0x80;
        int material = NnFile.DataBase + materialOffset;
        int state = NnFile.DataBase + stateOffset;
        int length = Math.Max(material + 0x1C, state + Math.Max(stateLength, 0));
        var data = new byte[length];
        WriteU32(data, material, materialFlags);
        WriteI32(data, material + 12, hasState ? stateOffset : 0);

        if (hasState && stateLength >= 4)
            WriteU32(data, state, logic);
        if (hasState && stateLength >= 8)
        {
            WriteU16(data, state + 4, 5);
            WriteU16(data, state + 6, 2);
        }
        if (hasState && stateLength >= 22)
            WriteU16(data, state + 20, alphaComparison);
        if (hasState && stateLength >= 24)
            WriteU16(data, state + 22, depthComparison);
        if (hasState && stateLength >= 28)
            WriteU32(data, state + 24, alphaReference);

        return NnMaterial.Parse(data, materialOffset, pointerFlags, []);
    }

    private static NnModel ModelWithState(
        uint materialFlags,
        uint logic,
        ushort alphaComparison,
        ushort depthComparison,
        uint alphaReference,
        uint colourFlags = 0,
        (float R, float G, float B, float A)? ambient = null,
        (float R, float G, float B, float A)? diffuse = null)
    {
        const int objectOffset = 0x100;
        const int materialPointerOffset = 0x180;
        const int materialDescriptorOffset = 0x190;
        const int stateOffset = 0x1C0;
        const int vertexPointerOffset = 0x200;
        const int vertexDescriptorOffset = 0x210;
        const int vertexDataOffset = 0x240;
        const int primitivePointerOffset = 0x280;
        const int primitiveDescriptorOffset = 0x290;
        const int stripCountsOffset = 0x2B0;
        const int indicesOffset = 0x2C0;
        const int subObjectOffset = 0x2D0;
        const int meshSetOffset = 0x2F0;
        const int colourOffset = 0x320;

        var data = new byte[At(0x380)];
        WriteTag(data, 0, "NZIF");
        WriteTag(data, 8, "NZOB");
        WriteI32(data, 12, 8);
        WriteI32(data, 16, objectOffset);
        WriteTag(data, 24, "NEND");

        int header = At(objectOffset);
        WriteI32(data, header + 0x10, 1);
        WriteI32(data, header + 0x14, materialPointerOffset);
        WriteI32(data, header + 0x18, 1);
        WriteI32(data, header + 0x1C, vertexPointerOffset);
        WriteI32(data, header + 0x20, 1);
        WriteI32(data, header + 0x24, primitivePointerOffset);
        WriteI32(data, header + 0x38, 1);
        WriteI32(data, header + 0x3C, subObjectOffset);

        int materialPointer = At(materialPointerOffset);
        WriteU32(data, materialPointer, StandardShaderDescriptor);
        WriteI32(data, materialPointer + 4, materialDescriptorOffset);
        int material = At(materialDescriptorOffset);
        WriteU32(data, material, materialFlags);
        WriteI32(data, material + 8, colourOffset);
        WriteI32(data, material + 12, stateOffset);
        int state = At(stateOffset);
        WriteU32(data, state, logic);
        WriteU16(data, state + 4, 5);
        WriteU16(data, state + 6, 2);
        WriteU16(data, state + 20, alphaComparison);
        WriteU16(data, state + 22, depthComparison);
        WriteU32(data, state + 24, alphaReference);

        var ambientTerms = ambient ?? (0f, 0f, 0f, 1f);
        var diffuseTerms = diffuse ?? (1f, 1f, 1f, 1f);
        int colour = At(colourOffset);
        WriteU32(data, colour, colourFlags);
        WriteF32(data, colour + 4, ambientTerms.R);
        WriteF32(data, colour + 8, ambientTerms.G);
        WriteF32(data, colour + 12, ambientTerms.B);
        WriteF32(data, colour + 16, ambientTerms.A);
        WriteF32(data, colour + 20, diffuseTerms.R);
        WriteF32(data, colour + 24, diffuseTerms.G);
        WriteF32(data, colour + 28, diffuseTerms.B);
        WriteF32(data, colour + 32, diffuseTerms.A);

        int vertexPointer = At(vertexPointerOffset);
        WriteI32(data, vertexPointer + 4, vertexDescriptorOffset);
        int vertex = At(vertexDescriptorOffset);
        WriteU32(data, vertex, (uint)VertexFormat.Position);
        WriteI32(data, vertex + 8, 12);
        WriteI32(data, vertex + 12, 3);
        WriteI32(data, vertex + 16, vertexDataOffset);
        WriteF32(data, At(vertexDataOffset + 12), 1f);
        WriteF32(data, At(vertexDataOffset + 28), 1f);

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

        int subObject = At(subObjectOffset);
        WriteI32(data, subObject + 4, 1);
        WriteI32(data, subObject + 8, meshSetOffset);
        int mesh = At(meshSetOffset);
        WriteI32(data, mesh + 0x10, 0);
        WriteI32(data, mesh + 0x14, 0);
        WriteI32(data, mesh + 0x18, 0);
        WriteI32(data, mesh + 0x1C, 0);
        WriteI32(data, mesh + 0x20, 0);

        return NnModel.Load(data) ?? throw new InvalidOperationException("material fixture did not load");
    }

    private static int At(int relativeOffset) => NnFile.DataBase + relativeOffset;

    private static void WriteTag(byte[] data, int at, string tag) =>
        Encoding.ASCII.GetBytes(tag).CopyTo(data, at);

    private static void WriteF32(byte[] data, int at, float value) =>
        WriteI32(data, at, BitConverter.SingleToInt32Bits(value));

    private static void WriteI32(byte[] data, int at, int value) =>
        BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(at), value);

    private static void WriteU16(byte[] data, int at, ushort value) =>
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(at), value);

    private static void WriteU32(byte[] data, int at, uint value) =>
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(at), value);
}
