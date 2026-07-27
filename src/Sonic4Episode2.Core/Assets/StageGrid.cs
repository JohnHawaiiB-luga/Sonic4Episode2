using System.Buffers.Binary;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// One layer of a stage: a 2D grid of cells, <c>.MP</c> holding 16-bit cells
/// and <c>.MD</c> 8-bit ones.
/// </summary>
/// <remarks>
/// Documented in <c>docs/FORMAT-STAGEMAP.md</c>; all 400 grids in the build
/// resolve exactly against their header dimensions.
/// <code>
/// 0x00  u16  width
/// 0x02  u16  height
/// 0x04       width * height cells
/// </code>
/// </remarks>
public sealed class StageGrid
{
    private readonly ushort[] _cells;

    private StageGrid(string name, int width, int height, int depth, ushort[] cells)
    {
        Name = name;
        Width = width;
        Height = height;
        Depth = depth;
        _cells = cells;
    }

    public string Name { get; }
    public int Width { get; }
    public int Height { get; }

    /// <summary>Bytes per cell: 2 for <c>.MP</c>, 1 for <c>.MD</c>.</summary>
    public int Depth { get; }

    public ushort this[int x, int y] => _cells[y * Width + x];

    public static StageGrid Parse(string name, ReadOnlySpan<byte> data)
    {
        if (data.Length < 4)
            throw new AmbException($"{name}: too short for a grid header");

        int width = BinaryPrimitives.ReadUInt16LittleEndian(data);
        int height = BinaryPrimitives.ReadUInt16LittleEndian(data[2..]);
        int count = width * height;
        int body = data.Length - 4;

        ushort[] cells;
        int depth;
        if (count != 0 && body == count * 2)
        {
            depth = 2;
            cells = new ushort[count];
            for (int i = 0; i < count; i++)
                cells[i] = BinaryPrimitives.ReadUInt16LittleEndian(data[(4 + i * 2)..]);
        }
        else if (count != 0 && body == count)
        {
            depth = 1;
            cells = new ushort[count];
            for (int i = 0; i < count; i++)
                cells[i] = data[4 + i];
        }
        else
        {
            throw new AmbException(
                $"{name}: {width}x{height} does not divide {body} body bytes");
        }

        return new StageGrid(name, width, height, depth, cells);
    }

    /// <summary>
    /// Splits a <c>.MP</c> cell into its tile index and transform.
    /// </summary>
    /// <remarks>
    /// The cell is a bitfield: id in bits 0-11, rotation in 12-13, horizontal
    /// flip in 14, vertical flip in 15. Verified across 512,070 non-zero cells.
    /// Transforms are rare — 99.8% of cells carry none — but mirrored pairs do
    /// occur wherever level geometry is symmetric.
    /// </remarks>
    public TileRef Tile(int x, int y)
    {
        if (Depth != 2)
            throw new AmbException($"{Name}: tile decoding only applies to .MP grids");
        ushort v = this[x, y];
        return new TileRef(v & 0x0FFF, (v >> 12) & 3, (v & 0x4000) != 0, (v & 0x8000) != 0);
    }

    public double Occupancy
    {
        get
        {
            int used = 0;
            foreach (var cell in _cells)
                if (cell != 0) used++;
            return _cells.Length == 0 ? 0 : (double)used / _cells.Length;
        }
    }
}

/// <summary>A tile index into the zone's model archive, plus its transform.</summary>
public readonly record struct TileRef(int Id, int Rotation, bool FlipH, bool FlipV)
{
    public bool IsEmpty => Id == 0;
}
