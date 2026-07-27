using System.Numerics;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class RingFieldTests
{
    private static RingField Field(params Ring[] rings) => new(rings);

    [Fact]
    public void StagePixelsConvertToWorldWithYFlipped()
    {
        var field = Field(new Ring(64, 128));
        var at = field.WorldPosition(0);
        // 64 pixels is one cell across, so one CellSize along positive X, and the
        // grid's downward Y becomes negative world Y.
        Assert.Equal(StageAssembler.CellSize, at.X, precision: 4);
        Assert.Equal(-2f * StageAssembler.CellSize, at.Y, precision: 4);
    }

    [Fact]
    public void ARingUnderfootIsTaken()
    {
        var field = Field(new Ring(64, 128));
        Assert.Equal(1, field.Collect(field.WorldPosition(0)));
        Assert.Equal(1, field.Collected);
        Assert.Equal(0, field.Remaining);
        Assert.True(field.IsTaken(0));
    }

    [Fact]
    public void ARingIsOnlyTakenOnce()
    {
        var field = Field(new Ring(64, 128));
        var at = field.WorldPosition(0);
        Assert.Equal(1, field.Collect(at));
        Assert.Equal(0, field.Collect(at));
        Assert.Equal(1, field.Collected);
    }

    [Fact]
    public void ARingAcrossTheStageIsLeftAlone()
    {
        var field = Field(new Ring(64, 128));
        Assert.Equal(0, field.Collect(new Vector2(9999f, 9999f)));
        Assert.Equal(1, field.Remaining);
    }

    [Fact]
    public void AWholeRunIsTakenInOnePass()
    {
        var rings = Enumerable.Range(0, 5).Select(i => new Ring(64 + i, 128)).ToArray();
        var field = Field(rings);
        Assert.Equal(5, field.Collect(field.WorldPosition(2)));
    }

    [Fact]
    public void TheBodyReachesUpFromTheFeetNotDown()
    {
        // The player's origin is at its feet, so a ring above it is in range and
        // one the same distance below is not.
        float scale = PlayerPhysics.WorldPerPixel;
        var feet = new Vector2(0f, 0f);

        var above = Field(new Ring(0, -15));   // 15 px up in grid terms
        var below = Field(new Ring(0, 30));    // 30 px down

        Assert.Equal(1, above.Collect(feet));
        Assert.Equal(0, below.Collect(feet));
    }

    [Fact]
    public void ResetPutsThemBack()
    {
        var field = Field(new Ring(64, 128));
        field.Collect(field.WorldPosition(0));
        field.Reset();
        Assert.Equal(0, field.Collected);
        Assert.False(field.IsTaken(0));
    }
}
