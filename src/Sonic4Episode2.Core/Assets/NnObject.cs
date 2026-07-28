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
                         int stride, int count, int bufferOffset)
    {
        _data = data;
        Format = (VertexFormat)format;
        Unknown = unknown;
        Stride = stride;
        Count = count;
        BufferOffset = bufferOffset;
    }

    public VertexFormat Format { get; }

    /// <summary>Resolves near the descriptor itself; meaning unknown.</summary>
    public uint Unknown { get; }

    public int Stride { get; }
    public int Count { get; }
    public int BufferOffset { get; }

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
        return new NnVertexList(data,
            BinaryPrimitives.ReadUInt32LittleEndian(s[at..]),
            BinaryPrimitives.ReadUInt32LittleEndian(s[(at + 4)..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 8)..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 12)..]),
            BinaryPrimitives.ReadInt32LittleEndian(s[(at + 16)..]));
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
/// </remarks>
public sealed record NnNode(
    uint Flags, short MatrixIndex, short Parent, short Child, short Sibling,
    float TranslateX, float TranslateY, float TranslateZ,
    int RotateX, int RotateY, int RotateZ,
    float ScaleX, float ScaleY, float ScaleZ)
{
    public const int Size = 0x90;

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
        Le.F32(data, at + 0x24), Le.F32(data, at + 0x28), Le.F32(data, at + 0x2C));
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
    public int? TextureIndex { get; init; }

    public static NnMaterial Parse(ReadOnlySpan<byte> data, int offset,
                                   uint pointerFlags, HashSet<int> relocations)
    {
        int at = NnFile.DataBase + offset;
        int? texture = null;

        if (relocations.Contains(offset + 0x18))
        {
            int block = BinaryPrimitives.ReadInt32LittleEndian(data[(at + 0x18)..]);
            if (block != 0 && NnFile.DataBase + block + 8 <= data.Length)
                texture = BinaryPrimitives.ReadInt32LittleEndian(
                    data[(NnFile.DataBase + block + 4)..]);
        }

        return new NnMaterial
        {
            PointerFlags = pointerFlags,
            Flags = BinaryPrimitives.ReadUInt32LittleEndian(data[at..]),
            ColourOffset = BinaryPrimitives.ReadInt32LittleEndian(data[(at + 8)..]),
            StateOffset = BinaryPrimitives.ReadInt32LittleEndian(data[(at + 12)..]),
            TextureIndex = texture,
        };
    }
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
