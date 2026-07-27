using System.Buffers.Binary;
using System.Text;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Reader for SEGA NN "BINCNK" files — models (<c>.ZNO</c>), motions
/// (<c>.ZNM</c>) and morph animation (<c>.ZNV</c>).
/// </summary>
/// <remarks>
/// A flat sequence of <c>tag[4] + u32 size</c> chunks running to <c>NEND</c>.
/// The tag's second letter is the platform: <c>Z</c> Direct3D 9, <c>X</c> Xbox,
/// <c>G</c> GameCube, <c>I</c> OpenGL ES.
/// <para>
/// The single most important rule in this format: <b>every internal offset is
/// relative to <c>OfsData</c></b> (0x20 in practice), not to the chunk and not
/// to the file. Episode II relocates pointers in place at load time via
/// <c>NOF0</c> rather than re-parsing, which is why the file layout <i>is</i> the
/// in-memory layout. Getting that base wrong yields a plausible-looking parse of
/// nonsense.
/// </para>
/// Full specification in <c>docs/FORMAT-NN.md</c>.
/// </remarks>
public sealed class NnFile
{
    /// <summary>Where all internal offsets are measured from.</summary>
    public const int DataBase = 0x20;

    private readonly ReadOnlyMemory<byte> _data;

    private NnFile(ReadOnlyMemory<byte> data, IReadOnlyList<NnChunk> chunks)
    {
        _data = data;
        Chunks = chunks;
    }

    public IReadOnlyList<NnChunk> Chunks { get; }

    public ReadOnlySpan<byte> Data => _data.Span;

    public static NnFile Parse(ReadOnlyMemory<byte> data)
    {
        var span = data.Span;
        if (span.Length < 8)
            throw new NnException("too short to hold a chunk header");

        string first = Tag(span);
        if (!first.EndsWith("IF", StringComparison.Ordinal) || first[0] != 'N')
            throw new NnException($"not an NN container (tag '{first}')");

        var chunks = new List<NnChunk>();
        int offset = 0;
        while (offset + 8 <= span.Length)
        {
            string tag = Tag(span[offset..]);
            int size = BinaryPrimitives.ReadInt32LittleEndian(span[(offset + 4)..]);
            if (size < 0 || offset + 8L + size > span.Length)
                throw new NnException($"chunk '{tag}' at {offset:X} overruns the buffer");

            chunks.Add(new NnChunk(tag, offset, size));
            if (tag == "NEND")
                return new NnFile(data, chunks);
            offset += 8 + size;
        }
        throw new NnException("ran off the end without an NEND chunk");
    }

    /// <summary>Finds a chunk by its platform-independent suffix, e.g. "OB".</summary>
    public NnChunk? FindBySuffix(string suffix)
    {
        foreach (var chunk in Chunks)
            if (chunk.Tag.Length == 4 && chunk.Tag.AsSpan(2).SequenceEqual(suffix))
                return chunk;
        return null;
    }

    public NnChunk? Find(string tag)
    {
        foreach (var chunk in Chunks)
            if (chunk.Tag == tag)
                return chunk;
        return null;
    }

    /// <summary>The original authored filename, from <c>NFN0</c>.</summary>
    /// <remarks>
    /// Worth having: the AMB string table uppercases names, while this preserves
    /// what the artist typed — <c>Z1_G_hasira_B.zno</c> against the archive's
    /// <c>Z1_G_HASIRA_B.ZNO</c>. Two reserved words precede the string.
    /// </remarks>
    public string? SourceName
    {
        get
        {
            var chunk = Find("NFN0");
            if (chunk is null || chunk.Size < 9) return null;
            var body = Data.Slice(chunk.Offset + 8 + 8, chunk.Size - 8);
            int end = body.IndexOf((byte)0);
            if (end >= 0) body = body[..end];
            string name = Encoding.ASCII.GetString(body);
            return name.Length == 0 ? null : name;
        }
    }

    /// <summary>The object header of a model, or null if this is not one.</summary>
    public NnObject? ReadObject()
    {
        var chunk = FindBySuffix("OB");
        if (chunk is null) return null;
        int at = DataBase + ChunkMainData(chunk);
        if (at < 0 || at + NnObject.Size > Data.Length)
            throw new NnException($"object header at {at:X} lies outside the file");
        return NnObject.Parse(Data, at);
    }

    /// <summary>The motion header and its channels, or null if this is not one.</summary>
    public (NnMotion Motion, IReadOnlyList<NnSubMotion> Channels)? ReadMotion()
    {
        var chunk = FindBySuffix("MO");
        if (chunk is null) return null;
        int at = DataBase + ChunkMainData(chunk);
        if (at < 0 || at + NnMotion.Size > Data.Length)
            throw new NnException($"motion header at {at:X} lies outside the file");

        var motion = NnMotion.Parse(Data, at);
        if (motion.SubMotionCount < 0)
            throw new NnException($"negative channel count {motion.SubMotionCount}");

        var channels = new List<NnSubMotion>(motion.SubMotionCount);
        if (motion.SubMotionCount > 0)
        {
            int array = DataBase + motion.SubMotionOffset;
            if (motion.SubMotionOffset == 0 ||
                array + (long)motion.SubMotionCount * NnSubMotion.Size > Data.Length)
                throw new NnException("channel array overruns the file");
            for (int i = 0; i < motion.SubMotionCount; i++)
                channels.Add(NnSubMotion.Parse(Data, array + i * NnSubMotion.Size));
        }
        return (motion, channels);
    }

    /// <summary>Texture filenames this model references, from <c>NZTL</c>.</summary>
    public IReadOnlyList<string> ReadTextureNames()
    {
        var chunk = FindBySuffix("TL");
        if (chunk is null) return Array.Empty<string>();

        int at = DataBase + ChunkMainData(chunk);
        if (at + 8 > Data.Length)
            throw new NnException($"texture list root at {at:X} outside the file");

        int count = BinaryPrimitives.ReadInt32LittleEndian(Data[at..]);
        int list = BinaryPrimitives.ReadInt32LittleEndian(Data[(at + 4)..]);
        if (count < 0 || DataBase + list + (long)count * 0x14 > Data.Length)
            throw new NnException($"texture list of {count} overruns the file");

        var names = new List<string>(count);
        for (int i = 0; i < count; i++)
        {
            int nameOffset = BinaryPrimitives.ReadInt32LittleEndian(
                Data[(DataBase + list + i * 0x14 + 4)..]);
            names.Add(nameOffset == 0 ? "" : ReadCString(DataBase + nameOffset));
        }
        return names;
    }

    /// <summary>
    /// Offsets the engine patches at load time, from <c>NOF0</c>.
    /// </summary>
    /// <remarks>
    /// Recovered from the loader in <c>Sonic.exe</c> at <c>0x006c6c33</c>: each
    /// listed byte offset is shifted right by two to index a word, and the data
    /// base is added to it in place.
    /// <para>
    /// This doubles as a map of <b>which words in the file are pointers</b>,
    /// which is the only thing that made the variable-size material struct
    /// readable after three size-based attempts failed.
    /// </para>
    /// </remarks>
    public HashSet<int> ReadRelocations()
    {
        var result = new HashSet<int>();
        var chunk = Find("NOF0");
        if (chunk is null || chunk.Size < 8) return result;

        int body = chunk.Offset + 8;
        int count = BinaryPrimitives.ReadInt32LittleEndian(Data[body..]);
        if (count < 0 || 8 + (long)count * 4 > chunk.Size)
            throw new NnException($"NOF0 declares {count} entries, chunk is {chunk.Size} bytes");

        for (int i = 0; i < count; i++)
        {
            int offset = BinaryPrimitives.ReadInt32LittleEndian(Data[(body + 8 + i * 4)..]);
            if ((offset & 3) != 0)
                throw new NnException($"relocation {offset:X} is not word aligned");
            if (DataBase + offset + 4 > Data.Length)
                throw new NnException($"relocation {offset:X} outside the file");
            result.Add(offset);
        }
        return result;
    }

    internal int ChunkMainData(NnChunk chunk)
    {
        if (chunk.Size < 8)
            throw new NnException($"chunk '{chunk.Tag}' too short for its data header");
        return BinaryPrimitives.ReadInt32LittleEndian(Data[(chunk.Offset + 8)..]);
    }

    internal string ReadCString(int at)
    {
        if (at < 0 || at >= Data.Length) return "";
        var span = Data[at..];
        int end = span.IndexOf((byte)0);
        if (end >= 0) span = span[..end];
        return Encoding.ASCII.GetString(span);
    }

    private static string Tag(ReadOnlySpan<byte> span) => Encoding.ASCII.GetString(span[..4]);
}

public sealed record NnChunk(string Tag, int Offset, int Size);

public sealed class NnException(string message) : Exception(message);
