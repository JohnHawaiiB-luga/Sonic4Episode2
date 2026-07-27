using System.Buffers.Binary;
using System.Text;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Reader for the AliceNN <c>AMB</c> container, which holds every model,
/// texture, animation and shader in the game.
/// </summary>
/// <remarks>
/// Layout is documented in <c>docs/FORMAT-AMB.md</c> and verified against all
/// 1,614 archives in the Beta 8 build.
/// <code>
/// 0x00  char[4]  '#AMB'
/// 0x10  s32      entry count
/// 0x14  s32      entry table offset   (16 bytes each: offset, length, pad)
/// 0x1C  s32      string table offset  (32 bytes each, NUL terminated)
/// </code>
/// Entries are read as slices over the original buffer rather than copies, so
/// mounting an archive costs one read and nested archives cost nothing extra.
/// </remarks>
public sealed class AmbArchive
{
    private static ReadOnlySpan<byte> Magic => "#AMB"u8;

    private const int EntrySize = 0x10;
    private const int NameSize = 0x20;

    private readonly ReadOnlyMemory<byte> _data;

    private AmbArchive(ReadOnlyMemory<byte> data, IReadOnlyList<AmbEntry> entries)
    {
        _data = data;
        Entries = entries;
    }

    public IReadOnlyList<AmbEntry> Entries { get; }

    public int Count => Entries.Count;

    public static AmbArchive Load(string path) => Parse(File.ReadAllBytes(path));

    public static AmbArchive Parse(ReadOnlyMemory<byte> data)
    {
        var span = data.Span;
        if (span.Length < 0x20 || !span[..4].SequenceEqual(Magic))
            throw new AmbException("not an AMB archive");

        int count = BinaryPrimitives.ReadInt32LittleEndian(span[0x10..]);
        int entryTable = BinaryPrimitives.ReadInt32LittleEndian(span[0x14..]);
        int stringTable = BinaryPrimitives.ReadInt32LittleEndian(span[0x1C..]);

        if (count < 0 || entryTable < 0)
            throw new AmbException($"corrupt header: {count} entries at {entryTable:X}");
        if (entryTable + (long)count * EntrySize > span.Length)
            throw new AmbException("entry table overruns the buffer");

        var entries = new List<AmbEntry>(count);
        for (int i = 0; i < count; i++)
        {
            var record = span[(entryTable + i * EntrySize)..];
            int offset = BinaryPrimitives.ReadInt32LittleEndian(record);
            int length = BinaryPrimitives.ReadInt32LittleEndian(record[4..]);

            string name = "";
            if (stringTable != 0)
            {
                int at = stringTable + i * NameSize;
                if (at + NameSize <= span.Length)
                    name = ReadCString(span.Slice(at, NameSize));
            }

            // Stage geometry archives carry a string table whose slots are
            // mostly blank; falling back to the index keeps every entry
            // distinct. Without this, extraction silently loses about a
            // quarter of the stage data to filename collisions.
            entries.Add(new AmbEntry(i, name.Length == 0 ? i.ToString() : name, offset, length));
        }

        return new AmbArchive(data, entries);
    }

    /// <summary>The bytes of one entry, as a slice over the archive buffer.</summary>
    public ReadOnlyMemory<byte> Read(AmbEntry entry)
    {
        if (entry.Offset < 0 || entry.Offset + (long)entry.Length > _data.Length)
            throw new AmbException($"entry '{entry.Name}' lies outside the archive");
        return _data.Slice(entry.Offset, entry.Length);
    }

    public AmbEntry? Find(string name)
    {
        foreach (var entry in Entries)
            if (string.Equals(entry.Name, name, StringComparison.OrdinalIgnoreCase))
                return entry;
        return null;
    }

    /// <summary>Parses an entry that is itself an archive.</summary>
    public AmbArchive OpenNested(AmbEntry entry) => Parse(Read(entry));

    /// <summary>Structural problems, or empty when the archive is self-consistent.</summary>
    public IReadOnlyList<string> Validate()
    {
        var problems = new List<string>();
        foreach (var entry in Entries)
        {
            if (entry.Offset < 0 || entry.Length < 0)
                problems.Add($"[{entry.Index}] {entry.Name}: negative offset or length");
            else if (entry.Offset + (long)entry.Length > _data.Length)
                problems.Add($"[{entry.Index}] {entry.Name}: extends past end of archive");
        }
        return problems;
    }

    private static string ReadCString(ReadOnlySpan<byte> span)
    {
        int end = span.IndexOf((byte)0);
        if (end >= 0)
            span = span[..end];
        return Encoding.ASCII.GetString(span);
    }
}

public readonly record struct AmbEntry(int Index, string Name, int Offset, int Length)
{
    public bool IsArchive => Name.EndsWith(".amb", StringComparison.OrdinalIgnoreCase);
}

public sealed class AmbException(string message) : Exception(message);
