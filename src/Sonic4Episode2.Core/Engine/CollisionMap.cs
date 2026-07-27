using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// Per-cell solidity, built from a stage's <c>_ATTR_</c> layers.
/// </summary>
/// <remarks>
/// The attribute layers are a strict superset of the visual ones. In Zone 1
/// Act 1 every cell carrying a tile also carries an attribute, and a further
/// 1,285 cells carry an attribute with no tile at all — invisible walls,
/// ceilings and blockers. Collision therefore has to come from `_ATTR_`, not
/// from what you can see.
/// <para>
/// When a stage's <c>.DF</c> shape file is supplied, the ground follows its real
/// per-column heights, so slopes and curves work. Without one the map falls back
/// to treating any non-zero attribute as fully solid, which is correct for flat
/// ground and walls and wrong on everything shaped.
/// </para>
/// </remarks>
public sealed class CollisionMap
{
    private readonly bool[] _solid;
    private readonly ushort[] _attribute;
    private readonly CollisionShapes? _shapes;

    private CollisionMap(int width, int height, bool[] solid,
                         ushort[] attribute, CollisionShapes? shapes)
    {
        Width = width;
        Height = height;
        _solid = solid;
        _attribute = attribute;
        _shapes = shapes;
    }

    /// <summary>True when real height fields are driving the ground.</summary>
    public bool HasShapes => _shapes is not null;

    public int Width { get; }
    public int Height { get; }

    /// <summary>Cell size in world units, matching <see cref="StageAssembler.CellSize"/>.</summary>
    public float CellSize => StageAssembler.CellSize;

    public static CollisionMap FromGrid(StageGrid attributes, CollisionShapes? shapes = null)
    {
        int count = attributes.Width * attributes.Height;
        var solid = new bool[count];
        var ids = new ushort[count];
        for (int y = 0; y < attributes.Height; y++)
        for (int x = 0; x < attributes.Width; x++)
        {
            int at = y * attributes.Width + x;
            ushort raw = attributes[x, y];
            // Transform bits live in the top nibble, same as a tile cell.
            ids[at] = (ushort)(raw & 0x0FFF);
            solid[at] = raw != 0;
        }
        return new CollisionMap(attributes.Width, attributes.Height, solid, ids, shapes);
    }

    /// <summary>The attribute id of a cell, or 0 when empty or out of bounds.</summary>
    public int AttributeAt(int cellX, int cellY)
    {
        if (cellX < 0 || cellX >= Width || cellY < 0 || cellY >= Height) return 0;
        return _attribute[cellY * Width + cellX];
    }

    public bool IsSolid(int cellX, int cellY)
    {
        // Outside the grid horizontally is open space; below it is solid so a
        // player cannot fall out of the world during early testing.
        if (cellX < 0 || cellX >= Width) return false;
        if (cellY < 0) return false;
        if (cellY >= Height) return true;
        return _solid[cellY * Width + cellX];
    }

    /// <summary>Converts a world position to the cell containing it.</summary>
    /// <remarks>
    /// Grid Y grows downward while world Y grows upward, which is the sign flip
    /// that catches everyone once.
    /// </remarks>
    public (int X, int Y) CellAt(float worldX, float worldY) =>
        ((int)MathF.Floor(worldX / CellSize), (int)MathF.Floor(-worldY / CellSize));

    public bool IsSolidAt(float worldX, float worldY)
    {
        var (x, y) = CellAt(worldX, worldY);
        return IsSolid(x, y);
    }

    /// <summary>
    /// Surface height of the ground beneath a point, or null within
    /// <paramref name="maxCells"/> cells if there is none.
    /// </summary>
    public float? GroundHeightAt(float worldX, float worldY, int maxCells = 4)
    {
        var (cellX, cellY) = CellAt(worldX, worldY);
        for (int i = 0; i <= maxCells; i++)
        {
            int y = cellY + i;
            if (!IsSolid(cellX, y)) continue;

            if (_shapes is null)
                return -y * CellSize;          // flat cell top

            int height = SampleHeight(cellX, y, worldX);
            if (height <= 0) continue;         // shaped cell that is empty here

            // Heights run 0 (cell floor) to 32 (cell ceiling), and world Y grows
            // upward while the grid grows downward, so the surface sits that
            // fraction above the cell's bottom edge.
            float bottom = -(y + 1) * CellSize;
            return bottom + height / (float)CollisionShapes.FullHeight * CellSize;
        }
        return null;
    }

    /// <summary>The height field's value at a world X within one cell.</summary>
    private int SampleHeight(int cellX, int cellY, float worldX)
    {
        if (_shapes is null) return CollisionShapes.FullHeight;

        int attribute = AttributeAt(cellX, cellY);
        if (attribute == 0) return 0;

        // A record holds an 8x8 block of cells; this is the cell's slot in it.
        int cell = (cellY & 7) * 8 + (cellX & 7);

        float local = worldX - cellX * CellSize;
        int column = (int)(local / CellSize * CollisionShapes.HeightsPerCell);
        column = Math.Clamp(column, 0, CollisionShapes.HeightsPerCell - 1);

        return _shapes.Height(attribute, cell, column);
    }
}
