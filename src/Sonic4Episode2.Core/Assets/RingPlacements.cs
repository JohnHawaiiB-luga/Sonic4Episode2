using System.Buffers.Binary;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Ring positions for a stage, from its <c>.RG</c> file.
/// </summary>
/// <remarks>
/// Rings are not objects. They have no entry in the <c>.EV</c> placement table
/// and no id in <see cref="ObjectCatalog"/> — they get their own file, and a
/// record is nothing but a position, because the type is implicit in which file
/// it came from.
/// <para>
/// The block grid is identical to <see cref="EventPlacements"/>; only the record
/// stride differs. What identifies the file as rings is the counts: acts carry
/// 192 to 489 records, boss arenas carry 12, and cutscenes carry none. Nothing
/// else in a Sonic act is numerous in that particular way.
/// </para>
/// </remarks>
public sealed class RingPlacements
{
    /// <summary>Bytes per record — just the position.</summary>
    public const int RecordStride = 2;

    private RingPlacements(int blockWidth, int blockHeight, IReadOnlyList<Ring> items)
    {
        BlockWidth = blockWidth;
        BlockHeight = blockHeight;
        Items = items;
    }

    public int BlockWidth { get; }
    public int BlockHeight { get; }
    public IReadOnlyList<Ring> Items { get; }

    public static RingPlacements Parse(ReadOnlySpan<byte> data)
    {
        var (width, height, records) = BlockGrid.Walk(data, RecordStride);
        var items = new List<Ring>(records.Count);
        foreach (var (blockX, blockY, at) in records)
            items.Add(new Ring(blockX * EventPlacements.BlockPitch + data[at],
                               blockY * EventPlacements.BlockPitch + data[at + 1]));
        return new RingPlacements(width, height, items);
    }
}

/// <summary>One ring, in stage pixels with Y growing downward as the grid does.</summary>
public readonly record struct Ring(int X, int Y);

/// <summary>
/// The block grid shared by <c>.EV</c>, <c>.DC</c> and <c>.RG</c>.
/// </summary>
/// <remarks>
/// All three are a <c>u16</c> grid size, then one absolute file offset per block,
/// then at each offset a <c>u16</c> count followed by that many fixed-size
/// records. Only the stride differs. Keeping the walk in one place matters
/// because the bounds checks are the only thing standing between a malformed
/// file and a wild read.
/// </remarks>
internal static class BlockGrid
{
    internal static (int Width, int Height, List<(int BlockX, int BlockY, int At)> Records)
        Walk(ReadOnlySpan<byte> data, int stride)
    {
        if (data.Length < 4)
            throw new AmbException("too short for a block grid header");

        int width = BinaryPrimitives.ReadUInt16LittleEndian(data);
        int height = BinaryPrimitives.ReadUInt16LittleEndian(data[2..]);
        long tableEnd = 4L + (long)width * height * 4;
        if (tableEnd > data.Length)
            throw new AmbException($"offset table {width}x{height} overruns the file");

        var records = new List<(int, int, int)>();
        for (int index = 0; index < width * height; index++)
        {
            int offset = BinaryPrimitives.ReadInt32LittleEndian(data[(4 + index * 4)..]);
            if (offset < 0 || offset + 2 > data.Length)
                throw new AmbException($"block {index} offset {offset} out of range");

            int count = BinaryPrimitives.ReadUInt16LittleEndian(data[offset..]);
            if (offset + 2 + (long)count * stride > data.Length)
                throw new AmbException($"block {index} declares {count} records but overruns");

            int blockY = Math.DivRem(index, width, out int blockX);
            for (int i = 0; i < count; i++)
                records.Add((blockX, blockY, offset + 2 + i * stride));
        }
        return (width, height, records);
    }
}
