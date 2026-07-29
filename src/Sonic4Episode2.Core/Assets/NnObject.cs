using System.Buffers.Binary;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Little-endian field reads.
/// </summary>
/// <remarks>
/// These exist as static helpers rather than local functions because a
/// <c>ReadOnlySpan</c> is ref-like and cannot be captured by a lambda or local
/// function.
/// </remarks>
internal static class Le
{
    public static float F32(ReadOnlySpan<byte> d, int at) =>
        BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(d[at..]));

    public static int I32(ReadOnlySpan<byte> d, int at) =>
        BinaryPrimitives.ReadInt32LittleEndian(d[at..]);

    public static uint U32(ReadOnlySpan<byte> d, int at) =>
        BinaryPrimitives.ReadUInt32LittleEndian(d[at..]);

    public static short I16(ReadOnlySpan<byte> d, int at) =>
        BinaryPrimitives.ReadInt16LittleEndian(d[at..]);
}

/// <summary>The <c>NZOB</c> object header: 88 bytes describing a model.</summary>
/// <remarks>
/// Every <c>*Offset</c> is relative to <see cref="NnFile.DataBase"/>. Zero means
/// the list is absent. Verified across all 3,577 models in the build.
/// </remarks>
public sealed record NnObject(
    float CenterX, float CenterY, float CenterZ, float Radius,
    int MaterialCount, int MaterialOffset,
    int VertexListCount, int VertexListOffset,
    int PrimitiveListCount, int PrimitiveListOffset,
    int NodeCount, int MaxNodeDepth, int NodeOffset,
    int MatrixPaletteCount, int SubObjectCount, int SubObjectOffset,
    int TextureCount, uint Flags, int Version,
    float BoundsX, float BoundsY, float BoundsZ)
{
    public const int Size = 0x58;

    /// <summary>A node tree deeper than one level means a real skeleton.</summary>
    public bool IsSkinned => NodeCount > 1 && MaxNodeDepth > 1;

    /// <summary>
    /// A null object: nodes but no geometry, used as a positional marker.
    /// </summary>
    /// <remarks>
    /// Cutscenes anchor their camera and actors to these — <c>CAMERA_POS.ZNO</c>,
    /// <c>SONIC_POS.ZNO</c>, <c>TAILS_POS.ZNO</c>. 31 exist in the build. A reader
    /// that rejects geometry-less objects throws away the cutscene camera rig.
    /// </remarks>
    public bool IsLocator => VertexListCount == 0 && PrimitiveListCount == 0 && NodeCount > 0;

    public static NnObject Parse(ReadOnlySpan<byte> data, int at) => new(
        Le.F32(data, at + 0x00), Le.F32(data, at + 0x04),
        Le.F32(data, at + 0x08), Le.F32(data, at + 0x0C),
        Le.I32(data, at + 0x10), Le.I32(data, at + 0x14),
        Le.I32(data, at + 0x18), Le.I32(data, at + 0x1C),
        Le.I32(data, at + 0x20), Le.I32(data, at + 0x24),
        Le.I32(data, at + 0x28), Le.I32(data, at + 0x2C), Le.I32(data, at + 0x30),
        Le.I32(data, at + 0x34), Le.I32(data, at + 0x38), Le.I32(data, at + 0x3C),
        Le.I32(data, at + 0x40), Le.U32(data, at + 0x44), Le.I32(data, at + 0x48),
        Le.F32(data, at + 0x4C), Le.F32(data, at + 0x50), Le.F32(data, at + 0x54));
}

/// <summary>Vertex attribute bits. Each combination accounts for its stride exactly.</summary>
[Flags]
public enum VertexFormat : uint
{
    Position = 0x00001,   // 3 floats
    Normal = 0x00002,     // 3 floats
    Diffuse = 0x00008,    // 4 bytes
    Specular = 0x00010,   // 4 bytes

    // Skinning, immediately after the position. Every skinned list in the build
    // carries all three weights; the index dword is optional.
    Weight1 = 0x01000,
    Weight2 = 0x02000,
    Weight3 = 0x04000,

    /// <summary>Four bone indices packed one per byte, a D3D <c>UBYTE4</c>.</summary>
    BlendIndices = 0x00400,

    TexCoord = 0x10000,
}

/// <summary>One <c>NZOB</c> vertex buffer.</summary>
public sealed class NnVertexList
{
    public const int Size = 0x14;

    private readonly ReadOnlyMemory<byte> _data;

    private NnVertexList(ReadOnlyMemory<byte> data, uint format, uint unknown,
                         int stride, int count, int bufferOffset,
                         int[] matrixIndices)
    {
        _data = data;
        Format = (VertexFormat)format;
        Unknown = unknown;
        Stride = stride;
        Count = count;
        BufferOffset = bufferOffset;
        MatrixIndices = matrixIndices;
    }

    public VertexFormat Format { get; }

    /// <summary>A secondary format word, mirroring the blend-index bit.</summary>
    public uint Unknown { get; }

    public int Stride { get; }
    public int Count { get; }
    public int BufferOffset { get; }

    /// <summary>
    /// The bone subset this list draws with: palette slots, in blend-index order.
    /// </summary>
    /// <remarks>
    /// The count sits at descriptor <c>+0x14</c> and the slot array behind the
    /// pointer at <c>+0x18</c> — the D3D9 shape of the <c>nMatrix</c> /
    /// <c>pMatrixIndices</c> pair Episode I's GL descriptor carries. A vertex's
    /// <c>UBYTE4</c> blend index selects into this list, and the list holds the
    /// global palette slot. Never longer than 16 (the shader's register budget);
    /// lists with weights but no per-vertex indices never exceed 4, so their
    /// implied indices are 0..3. Verified across every weighted list in the
    /// build: all 750 in range, max 16.
    /// </remarks>
    public IReadOnlyList<int> MatrixIndices { get; }

    /// <summary>
    /// Skinning weights per vertex, zero when the list is not skinned.
    /// </summary>
    /// <remarks>
    /// One float each, sitting immediately after the position and <b>before the
    /// normal</b> — which is the part that is easy to get wrong, because the
    /// normal is then not where a reader assuming position-then-normal looks.
    /// <para>
    /// Every skinned list in the build carries exactly three. Verified: 572 lists,
    /// and <b>96% of 112,831 sampled vertices have weights summing to 1.000</b>.
    /// </para>
    /// </remarks>
    public int WeightCount =>
        (Format.HasFlag(VertexFormat.Weight1) ? 1 : 0) +
        (Format.HasFlag(VertexFormat.Weight2) ? 1 : 0) +
        (Format.HasFlag(VertexFormat.Weight3) ? 1 : 0);

    /// <summary>Whether this list carries packed bone indices.</summary>
    /// <remarks>
    /// 177 of the 572 skinned lists do. The rest carry weights alone, so their
    /// bones must be implied by position in the mesh set's palette.
    /// </remarks>
    public bool HasBlendIndices => Format.HasFlag(VertexFormat.BlendIndices);

    /// <summary>Whether this list is skinned.</summary>
    public bool IsSkinned => WeightCount > 0;

    /// <summary>Byte offset of the weights within a vertex.</summary>
    public int WeightOffset => Format.HasFlag(VertexFormat.Position) ? 12 : 0;

    /// <summary>Byte offset of the packed indices, or -1 when there are none.</summary>
    public int BlendIndexOffset =>
        HasBlendIndices ? WeightOffset + WeightCount * 4 : -1;

    /// <summary>
    /// The four bone indices of one vertex, or all zero when the list carries none.
    /// </summary>
    /// <remarks>
    /// They are palette-relative, not node indices: the largest seen anywhere in
    /// the build is <b>15</b>, against models with up to 109 nodes. What maps them
    /// to nodes is the matrix palette, which is not decoded.
    /// </remarks>
    public (byte A, byte B, byte C, byte D) BlendIndices(int vertex)
    {
        int at = BlendIndexOffset;
        if (at < 0 || (uint)vertex >= (uint)Count) return (0, 0, 0, 0);
        var s = _data.Span;
        int start = NnFile.DataBase + BufferOffset + vertex * Stride + at;
        if (start < 0 || start + 4 > s.Length) return (0, 0, 0, 0);
        return (s[start], s[start + 1], s[start + 2], s[start + 3]);
    }

    /// <summary>The blend weights of one vertex, in order.</summary>
    public void ReadWeights(int vertex, Span<float> destination)
    {
        int n = Math.Min(WeightCount, destination.Length);
        var s = _data.Span;
        int start = NnFile.DataBase + BufferOffset + vertex * Stride + WeightOffset;
        for (int i = 0; i < n; i++)
            destination[i] = start + (i + 1) * 4 <= s.Length
                ? BitConverter.ToSingle(s[(start + i * 4)..])
                : 0f;
    }

    public static NnVertexList Parse(ReadOnlyMemory<byte> data, int at)
    {
        var s = data.Span;
        int[] matrixIndices = [];
        if (at + 0x1C <= s.Length)
        {
            int matrixCount = BinaryPrimitives.ReadInt32LittleEndian(s[(at + 0x14)..]);
            int matrixOffset = BinaryPrimitives.ReadInt32LittleEndian(s[(at + 0x18)..]);
            int target = NnFile.DataBase + matrixOffset;
            if (matrixCount is > 0 and <= 16 && matrixOffset > 0 &&
                target + matrixCount * 4 <= s.Length)
            {
                matrixIndices = new int[matrixCount];
                for (int i = 0; i < matrixCount; i++)
                    matrixIndices[i] =
                        BinaryPrimitives.ReadInt32LittleEndian(s[(target + i * 4)..]);
            }
        }
        return new NnVertexList(data,
            BinaryPrimitives.ReadUInt32LittleEndian(s[at..]),
            BinaryPrimitives.ReadUInt32LittleEndian(s[(at + 4)..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 8)..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 12)..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 16)..]),
            matrixIndices);
    }

    /// <summary>
    /// Byte offset of an attribute within a vertex, or -1 when absent.
    /// </summary>
    /// <remarks>
    /// Attributes are packed in a fixed order with no padding, which is why each
    /// observed flag combination accounts for its stride to the byte.
    /// </remarks>
    public int AttributeOffset(VertexFormat attribute)
    {
        if ((Format & attribute) == 0) return -1;
        int offset = 0;
        foreach (var (bit, size) in Layout)
        {
            if (bit == attribute) return offset;
            if ((Format & bit) != 0) offset += size;
        }
        return -1;
    }

    /// <summary>
    /// Component order within a vertex, which is not the order of the format bits.
    /// </summary>
    /// <remarks>
    /// <b>The skinning weights sit between the position and the normal.</b> Leaving
    /// them out does not just lose the weights, it puts every later component at
    /// the wrong offset — on Sonic the normal moves from 28 to 12 and the texture
    /// coordinates from 40 to 24. It reads as plausible garbage rather than as an
    /// error, which is how it went unnoticed.
    /// </remarks>
    private static readonly (VertexFormat Bit, int Size)[] Layout =
    [
        (VertexFormat.Position, 12),
        (VertexFormat.Weight1, 4), (VertexFormat.Weight2, 4),
        (VertexFormat.Weight3, 4), (VertexFormat.BlendIndices, 4),
        (VertexFormat.Normal, 12),
        (VertexFormat.Diffuse, 4), (VertexFormat.Specular, 4),
        (VertexFormat.TexCoord, 8),
    ];

    public void ReadPositions(Span<float> destination) =>
        ReadAttribute(VertexFormat.Position, 3, destination);

    /// <summary>Texture coordinates, or false when the format carries none.</summary>
    public bool ReadTexCoords(Span<float> destination)
    {
        if (AttributeOffset(VertexFormat.TexCoord) < 0) return false;
        ReadAttribute(VertexFormat.TexCoord, 2, destination);
        return true;
    }

    /// <summary>Vertex normals, or false when the format carries none.</summary>
    /// <remarks>
    /// The renderer fed a constant forward normal before these were read, which
    /// made every surface take identical light and is a large part of why the
    /// stage looked flat. The engine's own shader takes them as
    /// <c>nnglaNormal</c> — see <c>docs/ORACLES.md</c>.
    /// </remarks>
    public bool ReadNormals(Span<float> destination)
    {
        if (AttributeOffset(VertexFormat.Normal) < 0) return false;
        ReadAttribute(VertexFormat.Normal, 3, destination);
        return true;
    }

    private void ReadAttribute(VertexFormat attribute, int components, Span<float> destination)
    {
        int at = AttributeOffset(attribute);
        if (at < 0) throw new NnException($"vertex list has no {attribute}");
        var s = _data.Span;
        int start = NnFile.DataBase + BufferOffset;
        if (start < 0 || start + (long)Stride * Count > s.Length)
            throw new NnException("vertex buffer lies outside the file");

        for (int i = 0; i < Count; i++)
        {
            int p = start + i * Stride + at;
            for (int c = 0; c < components; c++)
                destination[i * components + c] = BitConverter.Int32BitsToSingle(
                    BinaryPrimitives.ReadInt32LittleEndian(s[(p + c * 4)..]));
        }
    }
}

/// <summary>One <c>NZOB</c> index buffer. Every list in the build is a strip.</summary>
public sealed class NnPrimitiveList
{
    public const int Size = 0x14;

    /// <summary>The only mode present anywhere in the build.</summary>
    public const uint TriangleStrip = 0x4810;

    private readonly ReadOnlyMemory<byte> _data;

    private NnPrimitiveList(ReadOnlyMemory<byte> data, uint mode, int total,
                            int stripCount, int countsOffset, int indicesOffset)
    {
        _data = data;
        Mode = mode;
        Total = total;
        StripCount = stripCount;
        CountsOffset = countsOffset;
        IndicesOffset = indicesOffset;
    }

    public uint Mode { get; }
    public int Total { get; }
    public int StripCount { get; }
    public int CountsOffset { get; }
    public int IndicesOffset { get; }

    public static NnPrimitiveList Parse(ReadOnlyMemory<byte> data, int at)
    {
        var s = data.Span;
        return new NnPrimitiveList(data,
            BinaryPrimitives.ReadUInt32LittleEndian(s[at..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 4)..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 8)..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 12)..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 16)..]));
    }

    /// <summary>Expands every strip to triangles, dropping degenerate stitches.</summary>
    public List<(int A, int B, int C)> Triangles()
    {
        var s = _data.Span;
        int countsAt = NnFile.DataBase + CountsOffset;
        if (countsAt + (long)StripCount * 4 > s.Length)
            throw new NnException("strip count table outside the file");

        var result = new List<(int, int, int)>();
        int at = NnFile.DataBase + IndicesOffset;
        for (int strip = 0; strip < StripCount; strip++)
        {
            int n = BinaryPrimitives.ReadInt32LittleEndian(s[(countsAt + strip * 4)..]);
            if (n < 0 || at + (long)n * 2 > s.Length)
                throw new NnException("index data outside the file");

            for (int i = 0; i + 2 < n; i++)
            {
                int a = BinaryPrimitives.ReadUInt16LittleEndian(s[(at + i * 2)..]);
                int b = BinaryPrimitives.ReadUInt16LittleEndian(s[(at + (i + 1) * 2)..]);
                int c = BinaryPrimitives.ReadUInt16LittleEndian(s[(at + (i + 2) * 2)..]);
                if (a == b || b == c || a == c) continue;  // stitch between strips
                result.Add((i % 2 == 0) ? (a, b, c) : (a, c, b));
            }
            at += n * 2;
        }
        return result;
    }
}

/// <summary>Binds a vertex list to a primitive list, a material and a node.</summary>
/// <remarks>
/// <b>40 bytes, where Episode I's <c>NNS_MESHSET</c> is 48.</b> Vertex and
/// primitive lists are <i>not</i> positionally paired — assuming they are fails
/// on roughly half the corpus with plausible-looking out-of-range indices.
/// </remarks>
public sealed record NnMeshSet(
    float CenterX, float CenterY, float CenterZ, float Radius,
    int NodeIndex, int MatrixIndex, int MaterialIndex,
    int VertexListIndex, int PrimitiveListIndex)
{
    public const int Size = 0x28;

    public static NnMeshSet Parse(ReadOnlySpan<byte> data, int at) => new(
        Le.F32(data, at + 0x00), Le.F32(data, at + 0x04),
        Le.F32(data, at + 0x08), Le.F32(data, at + 0x0C),
        Le.I32(data, at + 0x10), Le.I32(data, at + 0x14), Le.I32(data, at + 0x18),
        Le.I32(data, at + 0x1C), Le.I32(data, at + 0x20));
}

/// <summary>One node of the transform tree.</summary>
/// <remarks>
/// <b>144 bytes, where Episode I's <c>NNS_NODE</c> is 112.</b> Verified by
/// walking the tree on all 846 multi-node models: links in range, exactly one
/// root each, finite non-zero scales.
/// <para>
/// The 64 bytes at <c>+0x30</c> are the inverse bind matrix, recovered from
/// <c>nnCalcMatrixPaletteNode</c> in the symbolized Android build and verified
/// against this build's data: composing each node's bind world from its TRS
/// chain and multiplying by these bytes yields identity to 3e-6 on
/// <c>SON_SPINMODEL</c>'s whole skeleton. The flag bits carry Episode I's
/// <c>NND_NODETYPE_*</c> meanings: bits 0-2 declare the stored translation /
/// rotation / scale as identity (the engine skips the component without reading
/// it), bit 3 declares the inverse bind as identity (the palette copies the
/// world matrix untouched).
/// </para>
/// </remarks>
public sealed record NnNode(
    uint Flags, short MatrixIndex, short Parent, short Child, short Sibling,
    float TranslateX, float TranslateY, float TranslateZ,
    int RotateX, int RotateY, int RotateZ,
    float ScaleX, float ScaleY, float ScaleZ)
{
    public const int Size = 0x90;

    // NND_NODETYPE_* bits, named in Episode I's source and honored by the
    // palette walker in the Android build.
    public const uint UnitTranslation = 0x1;
    public const uint UnitRotation = 0x2;
    public const uint UnitScaling = 0x4;
    public const uint UnitInitMatrix = 0x8;
    public const uint RotateOrderMask = 0xF00;   // 0 = XYZ, the only value shipped

    /// <summary>Inverse bind matrix, row-major as stored.</summary>
    public System.Numerics.Matrix4x4 InverseBind { get; init; } =
        System.Numerics.Matrix4x4.Identity;

    /// <summary>Whether the palette should take the world matrix untouched.</summary>
    public bool HasUnitInverseBind => (Flags & UnitInitMatrix) != 0;

    /// <summary>
    /// Rotation units in a full turn. Angles are stored as signed integers, not
    /// floats — the A16 convention Episode I's <c>mtMathSin</c> uses.
    /// </summary>
    /// <remarks>
    /// Sonic's skeleton settles it: of 327 rotation words 129 are non-zero, they
    /// span -32768 to 19180, and the values that recur are 16384 and -32768 —
    /// exactly a quarter and a half turn. Read as floats the same bytes are
    /// denormals and NaNs.
    /// </remarks>
    public const int RotationUnitsPerTurn = 65536;

    public bool IsRoot => Parent == -1;

    /// <summary>This node's rotation in radians.</summary>
    public (float X, float Y, float Z) RotationRadians =>
        (RotateX * MathF.Tau / RotationUnitsPerTurn,
         RotateY * MathF.Tau / RotationUnitsPerTurn,
         RotateZ * MathF.Tau / RotationUnitsPerTurn);

    public static NnNode Parse(ReadOnlySpan<byte> data, int at) => new(
        Le.U32(data, at),
        Le.I16(data, at + 0x04), Le.I16(data, at + 0x06),
        Le.I16(data, at + 0x08), Le.I16(data, at + 0x0A),
        Le.F32(data, at + 0x0C), Le.F32(data, at + 0x10), Le.F32(data, at + 0x14),
        Le.I32(data, at + 0x18), Le.I32(data, at + 0x1C), Le.I32(data, at + 0x20),
        Le.F32(data, at + 0x24), Le.F32(data, at + 0x28), Le.F32(data, at + 0x2C))
    {
        InverseBind = new System.Numerics.Matrix4x4(
            Le.F32(data, at + 0x30), Le.F32(data, at + 0x34), Le.F32(data, at + 0x38), Le.F32(data, at + 0x3C),
            Le.F32(data, at + 0x40), Le.F32(data, at + 0x44), Le.F32(data, at + 0x48), Le.F32(data, at + 0x4C),
            Le.F32(data, at + 0x50), Le.F32(data, at + 0x54), Le.F32(data, at + 0x58), Le.F32(data, at + 0x5C),
            Le.F32(data, at + 0x60), Le.F32(data, at + 0x64), Le.F32(data, at + 0x68), Le.F32(data, at + 0x6C)),
    };
}

/// <summary>One texture stage of a material.</summary>
/// <remarks>
/// A 32-byte record: <c>u32 flags</c>, <c>u32 index</c> into the model's texture
/// list, then floats. The flags separate into families by observation across the
/// build's 14,357 stages — <c>0x60000002</c> is the diffuse base and is stage 0
/// on 8,808 materials, <c>0x60000004</c> an environment map usually in stage 2,
/// and the <c>0x000N000N</c> family (both halves equal, high nibble clear) sits
/// in odd slots repeating index 0 and appears to be inert padding rather than a
/// live stage. Roles beyond base and environment are <b>not yet confirmed</b>.
/// </remarks>
/// <param name="Flags">The stage's raw flag word.</param>
/// <param name="Index">Index into the model's texture list.</param>
public readonly record struct NnTextureStage(uint Flags, int Index)
{
    /// <summary>32 bytes, verified: 14,357 indices decoded, 0 out of range.</summary>
    public const int Size = 32;

    /// <summary>
    /// Whether this looks like a live stage rather than inert padding.
    /// </summary>
    /// <remarks>
    /// The padding family has a clear high nibble and mirrors its low half into
    /// its high half (<c>0x00010001</c>, <c>0x00020002</c>, <c>0x00030003</c>).
    /// Live stages set the top nibble (<c>0x6…</c> or <c>0x2…</c>).
    /// </remarks>
    public bool IsLive => (Flags & 0xF0000000u) != 0;

    /// <summary>The diffuse base map — the stage the renderer draws today.</summary>
    public bool IsBase => IsLive && (Flags & 0xFFFFu) == 2;

    /// <summary>An environment (reflection) map.</summary>
    public bool IsEnvironment => IsLive && (Flags & 0xFFFFu) == 4;
}

/// <summary>A material, and the texture it selects.</summary>
/// <remarks>
/// The one variable-size structure in this format, so it cannot be walked by
/// stride. Which optional fields are present is read from the <c>NOF0</c>
/// relocation table. The texture binding is an optional pointer at
/// <c>+0x18</c> to a block whose second word indexes the model's texture list —
/// verified on 9,431 of 9,431 materials that carry one.
/// </remarks>
public sealed class NnMaterial
{
    public uint PointerFlags { get; init; }
    public uint Flags { get; init; }
    public int ColourOffset { get; init; }
    public int StateOffset { get; init; }

    /// <summary>Index into the model's texture list, or null when untextured.</summary>
    /// <remarks>The base stage. See <see cref="Stages"/> for the rest.</remarks>
    public int? TextureIndex { get; init; }

    /// <summary>
    /// Every texture stage this material binds, in slot order.
    /// </summary>
    /// <remarks>
    /// The material declares a stage <b>count</b> at <c>+0x14</c> and points at an
    /// array of <b>32-byte</b> stage records at <c>+0x18</c>. Each record is
    /// <c>u32 flags</c>, <c>u32 index</c> into the model's texture list, then
    /// floats. Verified across the build: <b>14,357 stage indices, 0 out of
    /// range</b>, and the per-count totals sum to the material count exactly —
    /// 6,967 materials with one stage, 987 with two, 617 with three, 735 with
    /// four, 125 with five and 336 untextured, which is 9,767.
    /// <para>
    /// <b>2,464 materials bind more than one texture</b> and the renderer drew
    /// only the first, because the decoder read a single index. The engine's own
    /// shader has nine sampler slots — base, three decals, modulate, add,
    /// opacity, normal and two user samplers (<c>docs/ORACLES.md</c>).
    /// </para>
    /// </remarks>
    public IReadOnlyList<NnTextureStage> Stages { get; init; } = [];

    /// <summary>How this material blends over what is already drawn.</summary>
    public MaterialBlend Blend { get; init; }

    /// <summary>
    /// The material's ambient term, RGBA.
    /// </summary>
    /// <remarks>
    /// The colour block holds exactly two RGBA colours — <b>all 9,767 materials
    /// in the build, no exceptions</b> — and this is the first. It clusters hard
    /// on uniform grey (0.3,0.3,0.3 on 4,859 materials) or black (2,792), which
    /// is an ambient-level signature rather than a surface colour.
    /// </remarks>
    public (float R, float G, float B, float A) Ambient { get; init; } = (0, 0, 0, 1);

    /// <summary>
    /// The material's diffuse term, RGBA. Modulates the texture.
    /// </summary>
    /// <remarks>
    /// The second colour of the block. <b>Pure white on 87.6% of materials</b> —
    /// i.e. "show the texture unchanged" — while its <b>alpha carries per-material
    /// transparency</b> (1.0 on 9,154, but 0.0, 0.25, 0.41, 0.6 and 0.98 all
    /// occur). White-with-meaningful-alpha is the classic diffuse signature, and
    /// it matches <c>nngluFrontMaterialDiffuse</c> in the engine's own shader.
    /// </remarks>
    public (float R, float G, float B, float A) Diffuse { get; init; } = (1, 1, 1, 1);

    public static NnMaterial Parse(ReadOnlySpan<byte> data, int offset,
                                   uint pointerFlags, HashSet<int> relocations)
    {
        int at = NnFile.DataBase + offset;
        int? texture = null;

        // The render-state block is 16 u16s; words 2 and 3 are the D3D9 source
        // and destination blend factors. SRCALPHA/INVSRCALPHA (5,6) is ordinary
        // transparency; SRCALPHA/ONE (5,2) is additive, the glow blend 2,761
        // materials use. Read straight from the block at StateOffset.
        var blend = MaterialBlend.Alpha;
        int stateOffset = BinaryPrimitives.ReadInt32LittleEndian(data[(at + 12)..]);
        if (stateOffset > 0 && NnFile.DataBase + stateOffset + 8 <= data.Length)
        {
            int sb = NnFile.DataBase + stateOffset;
            int src = BinaryPrimitives.ReadUInt16LittleEndian(data[(sb + 4)..]);
            int dst = BinaryPrimitives.ReadUInt16LittleEndian(data[(sb + 6)..]);
            blend = (D3dBlend)dst == D3dBlend.One ? MaterialBlend.Additive
                  : MaterialBlend.Alpha;
        }

        // The stage count sits at +0x14 and the array of 32-byte stage records at
        // +0x18. Reading only the first record is what limited 2,464 multi-textured
        // materials to their base map.
        NnTextureStage[] stages = [];
        if (relocations.Contains(offset + 0x18))
        {
            int block = BinaryPrimitives.ReadInt32LittleEndian(data[(at + 0x18)..]);
            int count = BinaryPrimitives.ReadInt32LittleEndian(data[(at + 0x14)..]);
            int start = NnFile.DataBase + block;
            if (block != 0 && count is > 0 and <= 16 &&
                start + count * NnTextureStage.Size <= data.Length)
            {
                stages = new NnTextureStage[count];
                for (int s = 0; s < count; s++)
                {
                    int r = start + s * NnTextureStage.Size;
                    stages[s] = new NnTextureStage(
                        BinaryPrimitives.ReadUInt32LittleEndian(data[r..]),
                        BinaryPrimitives.ReadInt32LittleEndian(data[(r + 4)..]));
                }
            }
            if (block != 0 && start + 8 <= data.Length)
                texture = BinaryPrimitives.ReadInt32LittleEndian(data[(start + 4)..]);
        }

        // The colour block: u32 count, then that many RGBA quads. Count is 2 on
        // every material in the build — ambient first, diffuse second.
        var ambient = (0f, 0f, 0f, 1f);
        var diffuse = (1f, 1f, 1f, 1f);
        int colourOffset = BinaryPrimitives.ReadInt32LittleEndian(data[(at + 8)..]);
        if (colourOffset > 0 && NnFile.DataBase + colourOffset + 4 + 32 <= data.Length)
        {
            int cb = NnFile.DataBase + colourOffset;
            if (BinaryPrimitives.ReadUInt32LittleEndian(data[cb..]) >= 2)
            {
                ambient = (Le.F32(data, cb + 4), Le.F32(data, cb + 8),
                           Le.F32(data, cb + 12), Le.F32(data, cb + 16));
                diffuse = (Le.F32(data, cb + 20), Le.F32(data, cb + 24),
                           Le.F32(data, cb + 28), Le.F32(data, cb + 32));
            }
        }

        return new NnMaterial
        {
            PointerFlags = pointerFlags,
            Flags = BinaryPrimitives.ReadUInt32LittleEndian(data[at..]),
            ColourOffset = colourOffset,
            StateOffset = BinaryPrimitives.ReadInt32LittleEndian(data[(at + 12)..]),
            TextureIndex = texture,
            Stages = stages,
            Blend = blend,
            Ambient = ambient,
            Diffuse = diffuse,
        };
    }
}

/// <summary>How a material combines with what is already on screen.</summary>
public enum MaterialBlend
{
    /// <summary>Ordinary transparency — <c>SRCALPHA / INVSRCALPHA</c>.</summary>
    Alpha,

    /// <summary>Additive glow — <c>SRCALPHA / ONE</c>; godrays, shine, effects.</summary>
    Additive,
}

/// <summary>The D3D9 blend-factor values a material's state block stores.</summary>
internal enum D3dBlend
{
    One = 2,
    SrcAlpha = 5,
    InvSrcAlpha = 6,
}

/// <summary>An <c>NZMO</c> animation header.</summary>
/// <remarks>
/// Start frames may be <b>negative</b> — several Sonic transition animations
/// begin at -5 or -10 for blend pre-roll, which is legitimate.
/// </remarks>
public sealed record NnMotion(
    uint Flags, float Start, float End,
    int SubMotionCount, int SubMotionOffset, float FrameRate)
{
    public const int Size = 0x20;

    /// <summary>1 node, 2 camera, 4 light.</summary>
    public int ChannelKind => (int)(Flags & 31);

    public static NnMotion Parse(ReadOnlySpan<byte> data, int at) => new(
        Le.U32(data, at), Le.F32(data, at + 0x04), Le.F32(data, at + 0x08),
        Le.I32(data, at + 0x0C), Le.I32(data, at + 0x10), Le.F32(data, at + 0x14));
}

/// <summary>One animated channel of a motion.</summary>
public sealed record NnSubMotion(
    uint Flags, uint InterpolationType, int Target,
    float Start, float End, float StartKey, float EndKey,
    int KeyCount, int KeySize, int KeyOffset)
{
    public const int Size = 0x28;

    public static NnSubMotion Parse(ReadOnlySpan<byte> data, int at) => new(
        Le.U32(data, at), Le.U32(data, at + 4), Le.I32(data, at + 0x08),
        Le.F32(data, at + 0x0C), Le.F32(data, at + 0x10),
        Le.F32(data, at + 0x14), Le.F32(data, at + 0x18),
        Le.I32(data, at + 0x1C), Le.I32(data, at + 0x20), Le.I32(data, at + 0x24));
}
