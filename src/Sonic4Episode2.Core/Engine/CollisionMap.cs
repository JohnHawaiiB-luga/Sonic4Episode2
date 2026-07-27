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
/// <b>This is deliberately an approximation.</b> A non-zero attribute is treated
/// as fully solid, which gives blocky collision: correct for flat ground and
/// walls, wrong on slopes and curves. The real shape data lives in the `.DF`
/// files — 64 bytes per cell, one height byte per pixel — which are not decoded
/// yet. Everything here is structured so that swapping a height field in later
/// changes <see cref="GroundHeightAt"/> and nothing else.
/// </para>
/// </remarks>
public sealed class CollisionMap
{
    private readonly bool[] _solid;

    private CollisionMap(int width, int height, bool[] solid)
    {
        Width = width;
        Height = height;
        _solid = solid;
    }

    public int Width { get; }
    public int Height { get; }

    /// <summary>Cell size in world units, matching <see cref="StageAssembler.CellSize"/>.</summary>
    public float CellSize => StageAssembler.CellSize;

    public static CollisionMap FromGrid(StageGrid attributes)
    {
        var solid = new bool[attributes.Width * attributes.Height];
        for (int y = 0; y < attributes.Height; y++)
        for (int x = 0; x < attributes.Width; x++)
            solid[y * attributes.Width + x] = attributes[x, y] != 0;
        return new CollisionMap(attributes.Width, attributes.Height, solid);
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
            // The top of cell y in world space. Once .DF is decoded this is
            // where a per-pixel height lookup replaces the flat cell top.
            return -y * CellSize;
        }
        return null;
    }
}
