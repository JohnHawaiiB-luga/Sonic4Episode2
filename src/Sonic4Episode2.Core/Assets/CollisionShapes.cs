using System.Buffers.Binary;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// A stage's collision shape file — <c>.DF</c> heights, <c>.DI</c> angles or
/// <c>.AT</c> character attributes.
/// </summary>
/// <remarks>
/// <code>
/// 0x00  u16   chips
/// 0x02  u16   records
/// 0x04        records[records][size]
/// ...   u16   chipIndex[chips]     - an _ATTR_ cell id maps straight to a record
/// </code>
/// with <c>size</c> 4096 for <c>.DF</c> and 64 for the other two.
/// <para>
/// <b>Records come first; the index table is last.</b> The file size works out
/// either way round, so the order cannot be settled from the data — it comes from
/// the engine's own setup routine at <c>Sonic.exe:0x00560349</c>, which computes
/// the index address as <c>base + 4 + records * size</c> via <c>shl ecx, 0xc</c>
/// on the record count.
/// </para>
/// <para>
/// Verified: all 1,535 index entries are in range, and every one of the 256
/// attribute ids Zone 1 Act 1 uses resolves to a valid record.
/// </para>
/// </remarks>
public sealed class CollisionShapes
{
    /// <summary>Cells per record — an 8x8 block.</summary>
    public const int CellsPerRecord = 64;

    /// <summary>Column heights per cell, one per pixel column.</summary>
    public const int HeightsPerCell = 64;

    /// <summary>A full-height cell. Measured over 8.4M height bytes.</summary>
    public const int FullHeight = 32;

    /// <summary>
    /// Pixels per height unit. A cell is 64 columns wide but only 32 units tall,
    /// so heights are stored at half resolution.
    /// </summary>
    /// <remarks>
    /// This is not an assumption from the 64/32 ratio. Fitting <c>.DI</c>'s stored
    /// surface angles against slopes measured from <c>.DF</c> only agrees when
    /// heights are scaled by 2: across 23,474 shaped cells the median error is
    /// 5.7 degrees at this scale versus 16.9 at 1:1.
    /// </remarks>
    public const int PixelsPerHeightUnit = 2;

    /// <summary>Degrees per <c>.DI</c> angle unit — a byte spans a full turn.</summary>
    public const float DegreesPerAngleUnit = 360f / 256f;

    private readonly byte[] _data;
    private readonly int _recordSize;
    private readonly ushort[] _index;

    private CollisionShapes(byte[] data, int recordSize, int recordCount, ushort[] index)
    {
        _data = data;
        _recordSize = recordSize;
        RecordCount = recordCount;
        _index = index;
    }

    public int RecordCount { get; }
    public int ChipCount => _index.Length;

    public static CollisionShapes Parse(ReadOnlySpan<byte> data, int recordSize)
    {
        if (data.Length < 4)
            throw new AmbException("collision file too short for a header");

        int chips = BinaryPrimitives.ReadUInt16LittleEndian(data);
        int records = BinaryPrimitives.ReadUInt16LittleEndian(data[2..]);

        long expected = 4L + (long)records * recordSize + (long)chips * 2;
        if (expected != data.Length)
            throw new AmbException(
                $"collision layout mismatch: {chips} chips, {records} records " +
                $"implies {expected} bytes, file has {data.Length}");

        int tableAt = 4 + records * recordSize;
        var index = new ushort[chips];
        for (int i = 0; i < chips; i++)
            index[i] = BinaryPrimitives.ReadUInt16LittleEndian(data[(tableAt + i * 2)..]);

        return new CollisionShapes(data.ToArray(), recordSize, records, index);
    }

    /// <summary>The record an attribute id selects, or -1 when out of range.</summary>
    public int RecordFor(int attributeId)
    {
        if (attributeId < 0 || attributeId >= _index.Length) return -1;
        int record = _index[attributeId];
        return record < RecordCount ? record : -1;
    }

    /// <summary>
    /// Height of one pixel column of a cell, 0 (empty) to 32 (full).
    /// </summary>
    /// <param name="attributeId">The cell's <c>_ATTR_</c> id.</param>
    /// <param name="cell">Which of the record's 64 cells, 0-63.</param>
    /// <param name="column">Which pixel column of that cell, 0-63.</param>
    public int Height(int attributeId, int cell, int column)
    {
        int record = RecordFor(attributeId);
        if (record < 0 || _recordSize != 4096) return 0;
        if ((uint)cell >= CellsPerRecord || (uint)column >= HeightsPerCell) return 0;
        return _data[4 + record * _recordSize + cell * HeightsPerCell + column];
    }

    /// <summary>
    /// The stored surface angle of a cell, as the raw byte — a full turn per 256.
    /// </summary>
    /// <remarks>
    /// Only meaningful on a <c>.DI</c> file, whose 64-byte records hold one angle
    /// per cell rather than a height field.
    /// </remarks>
    public int AngleUnits(int attributeId, int cell)
    {
        int record = RecordFor(attributeId);
        if (record < 0 || _recordSize != CellsPerRecord) return 0;
        if ((uint)cell >= CellsPerRecord) return 0;
        return _data[4 + record * _recordSize + cell];
    }

    /// <summary>
    /// The surface angle of a cell in degrees, counter-clockwise from flat, in a
    /// Y-up frame. Flat ground reads 0.
    /// </summary>
    /// <remarks>
    /// The stored byte runs the other way, because the grid's Y grows downward
    /// while the angle is measured against a world whose Y grows up.
    /// </remarks>
    public float AngleDegrees(int attributeId, int cell)
    {
        float degrees = -AngleUnits(attributeId, cell) * DegreesPerAngleUnit;
        // Wrap to [-180, 180) so a gentle rise reads as a small positive number
        // rather than as something just under a full turn.
        if (degrees < -180f) degrees += 360f;
        if (degrees >= 180f) degrees -= 360f;
        return degrees;
    }
}
