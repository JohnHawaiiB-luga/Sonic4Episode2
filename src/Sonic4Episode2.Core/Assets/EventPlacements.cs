using System.Buffers.Binary;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Object placements for a stage, from its <c>.EV</c> files.
/// </summary>
/// <remarks>
/// The file is a spatial index, not a flat list: a block grid at a quarter of
/// the map's resolution holding absolute offsets, so the engine can spawn and
/// despawn by scroll position without scanning everything. Each block is a
/// <c>u16</c> count followed by that many 12-byte records.
/// <para>
/// One block covers <b>256 x 256 pixels</b>, so a record's absolute position is
/// <c>block * 256 + local</c>. Documented in <c>docs/FORMAT-EVENTS.md</c>;
/// all 65 <c>.EV</c> files in the build parse.
/// </para>
/// <para>
/// <b>What the object ids mean is still unknown.</b> The game holds roughly 298
/// object names as immediates inside each object's own code rather than in a
/// lookup table, so mapping id 724 to "ring" needs disassembly. Until then a
/// placement is a position and a number.
/// </para>
/// </remarks>
public sealed class EventPlacements
{
    /// <summary>Pixels covered by one index block.</summary>
    public const int BlockPitch = 256;

    private EventPlacements(int blockWidth, int blockHeight, IReadOnlyList<Placement> items)
    {
        BlockWidth = blockWidth;
        BlockHeight = blockHeight;
        Items = items;
    }

    public int BlockWidth { get; }
    public int BlockHeight { get; }
    public IReadOnlyList<Placement> Items { get; }

    public static EventPlacements Parse(ReadOnlySpan<byte> data)
    {
        if (data.Length < 8)
            throw new AmbException("too short for an .EV header");

        int blockWidth = BinaryPrimitives.ReadUInt16LittleEndian(data);
        int blockHeight = BinaryPrimitives.ReadUInt16LittleEndian(data[2..]);
        long tableEnd = 4L + (long)blockWidth * blockHeight * 4;
        if (tableEnd > data.Length)
            throw new AmbException($"offset table {blockWidth}x{blockHeight} overruns the file");

        var items = new List<Placement>();
        for (int index = 0; index < blockWidth * blockHeight; index++)
        {
            int offset = BinaryPrimitives.ReadInt32LittleEndian(data[(4 + index * 4)..]);
            if (offset < 0 || offset + 2 > data.Length)
                throw new AmbException($"block {index} offset {offset} out of range");

            int count = BinaryPrimitives.ReadUInt16LittleEndian(data[offset..]);
            if (offset + 2 + count * 12 > data.Length)
                throw new AmbException($"block {index} declares {count} records but overruns");

            int blockY = Math.DivRem(index, blockWidth, out int blockX);
            for (int i = 0; i < count; i++)
            {
                var record = data.Slice(offset + 2 + i * 12, 12);
                items.Add(new Placement(
                    blockX * BlockPitch + record[0],
                    blockY * BlockPitch + record[1],
                    BinaryPrimitives.ReadUInt16LittleEndian(record[2..]),
                    BinaryPrimitives.ReadUInt16LittleEndian(record[4..]),
                    BinaryPrimitives.ReadUInt16LittleEndian(record[10..])));
            }
        }
        return new EventPlacements(blockWidth, blockHeight, items);
    }
}

/// <summary>One placed object.</summary>
/// <param name="X">Absolute pixel position.</param>
/// <param name="Y">Absolute pixel position, growing downward as the grid does.</param>
/// <param name="ObjectId">Which object; the id-to-name table is not recovered.</param>
public readonly record struct Placement(int X, int Y, int ObjectId, int Flags, int Parameter);
