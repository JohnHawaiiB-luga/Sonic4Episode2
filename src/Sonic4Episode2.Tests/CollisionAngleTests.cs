using System.Buffers.Binary;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class CollisionAngleTests
{
    /// <summary>
    /// Builds a `.DI`-shaped file: one 64-byte record of per-cell angles, with a
    /// chip index that maps attribute id to record 0.
    /// </summary>
    private static CollisionShapes Angles(params byte[] cellAngles)
    {
        const int chips = 4, records = 1;
        var data = new byte[4 + records * CollisionShapes.CellsPerRecord + chips * 2];
        BinaryPrimitives.WriteUInt16LittleEndian(data, chips);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(2), records);
        cellAngles.CopyTo(data.AsSpan(4));
        return CollisionShapes.Parse(data, CollisionShapes.CellsPerRecord);
    }

    [Fact]
    public void FlatGroundReadsZero()
    {
        Assert.Equal(0f, Angles(0).AngleDegrees(attributeId: 0, cell: 0));
    }

    [Fact]
    public void AByteSpansAFullTurn()
    {
        // 64 units is a quarter turn; the sign flips because the grid's Y grows
        // downward and the angle is measured in a Y-up frame.
        Assert.Equal(-90f, Angles(64).AngleDegrees(0, 0), precision: 4);
        Assert.Equal(-180f, Angles(128).AngleDegrees(0, 0), precision: 4);
        Assert.Equal(360f / 256f, CollisionShapes.DegreesPerAngleUnit, precision: 6);
    }

    [Fact]
    public void EachCellOfARecordCarriesItsOwnAngle()
    {
        var a = Angles(0, 16, 32);
        Assert.Equal(0, a.AngleUnits(0, 0));
        Assert.Equal(16, a.AngleUnits(0, 1));
        Assert.Equal(32, a.AngleUnits(0, 2));
    }

    [Fact]
    public void HeightFilesDoNotAnswerAngleQueries()
    {
        // A .DF has 4096-byte records; asking it for an angle is a category error
        // and must read as flat rather than as a stray height byte.
        var heights = new byte[4 + 4096 + 2 * 2];
        BinaryPrimitives.WriteUInt16LittleEndian(heights, 2);
        BinaryPrimitives.WriteUInt16LittleEndian(heights.AsSpan(2), 1);
        heights[4] = 200;
        Assert.Equal(0, CollisionShapes.Parse(heights, 4096).AngleUnits(0, 0));
    }

    [Fact]
    public void AMapWithoutAngleDataSaysSoRatherThanGuessing()
    {
        var grid = new byte[4 + 2];
        BinaryPrimitives.WriteUInt16LittleEndian(grid, 1);
        BinaryPrimitives.WriteUInt16LittleEndian(grid.AsSpan(2), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(grid.AsSpan(4), 1);

        var map = CollisionMap.FromGrid(StageGrid.Parse("test", grid));
        Assert.False(map.HasAngles);
        Assert.Null(map.SurfaceAngleAt(0f, 0f));
    }

    [Fact]
    public void HeightsAreStoredAtHalfHorizontalResolution()
    {
        // 64 columns across, 32 units tall: the ratio is the whole reason the
        // angle fit needed a factor of two.
        Assert.Equal(2, CollisionShapes.PixelsPerHeightUnit);
        Assert.Equal(CollisionShapes.HeightsPerCell,
                     CollisionShapes.FullHeight * CollisionShapes.PixelsPerHeightUnit);
    }
}
