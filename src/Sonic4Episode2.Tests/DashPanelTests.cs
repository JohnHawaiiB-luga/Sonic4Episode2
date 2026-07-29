using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class DashPanelTests
{
    private static int SpeedId { get; } =
        ObjectCatalog.IdsOfClass("DashPanel").First();

    private static Player Grounded()
    {
        var grid = new byte[4 + 512 * 4 * 2];
        BinaryPrimitives.WriteUInt16LittleEndian(grid, 512);
        BinaryPrimitives.WriteUInt16LittleEndian(grid.AsSpan(2), 4);
        for (int i = 0; i < 512 * 4; i++) grid[4 + i * 2] = 1;
        var player = new Player(CollisionMap.FromGrid(StageGrid.Parse("t", grid)));
        player.PlaceOnGround(50f, 0f);
        player.Update();
        return player;
    }

    [Fact]
    public void OnlySpeedPlacementsBecomePanels()
    {
        var placements = new List<Placement>
        {
            new(100, 100, SpeedId, 0, 0),
            new(300, 100, 715, 0, 0),
        };
        Assert.Equal(1, new DashPanels(placements).Count);
    }

    [Fact]
    public void ABoostSetsTheRecoveredEpisodeTwoSpeed()
    {
        // 13.5 is now read from Episode II's own direction table at 0x0096C658,
        // not borrowed from Episode I - see tools/dispatch.py notes and beat 60.
        var player = Grounded();
        player.DashBoost(DashPanels.BoostPixels * PlayerPhysics.WorldPerPixel,
                         DashPanels.NoFrictionFrames);
        Assert.Equal(13.5f, DashPanels.BoostPixels);
        Assert.Equal(13.5f * PlayerPhysics.WorldPerPixel, player.Velocity.X, precision: 4);
    }

    [Fact]
    public void SpringDirectionsAreTheEightCompassAngles()
    {
        // GmGmkSpringInit indexes an A16 angle table; springs fire in eight
        // directions, at even 45-degree steps.
        Assert.Equal(8, Springs.DirectionAngles.Length);
        Assert.Equal(new[] { 0, 8192, 16384, 24576, 32768, 40960, 49152, 57344 },
                     Springs.DirectionAngles);
        for (int i = 1; i < Springs.DirectionAngles.Length; i++)
            Assert.Equal(8192, Springs.DirectionAngles[i] - Springs.DirectionAngles[i - 1]);
        Assert.Equal(9.0f, Springs.HorizontalSpeedCap);
    }

    [Fact]
    public void ABoostNeverSlowsAFasterPlayer()
    {
        var player = Grounded();
        player.Velocity.X = 100f;
        player.DashBoost(4f, DashPanels.NoFrictionFrames);
        Assert.Equal(100f, player.Velocity.X);
    }

    [Fact]
    public void AtRestTheBoostFollowsFacing()
    {
        var player = Grounded();
        player.InputX = -1f;
        player.Update();                       // face left
        player.InputX = 0f;
        for (int i = 0; i < 200; i++) player.Update();   // come to rest

        player.DashBoost(4f, DashPanels.NoFrictionFrames);
        Assert.True(player.Velocity.X < 0f);
    }

    [Fact]
    public void FrictionIsSuspendedForTheGrantedFrames()
    {
        var boosted = Grounded();
        boosted.DashBoost(4f, 12);
        var plain = Grounded();
        plain.Velocity.X = 4f;

        for (int i = 0; i < 10; i++) { boosted.Update(); plain.Update(); }
        // The boosted player has not paid friction yet; the plain one has.
        Assert.True(boosted.Velocity.X > plain.Velocity.X);
    }

    [Fact]
    public void PanelsRearmOnExitLikeSprings()
    {
        var panels = new DashPanels([new Placement(640, 640, SpeedId, 0, 0)]);
        var at = panels.PositionOf(0);
        Assert.NotNull(panels.Check(at));
        Assert.Null(panels.Check(at));
        Assert.Null(panels.Check(at + new Vector2(1000f, 0f)));
        Assert.NotNull(panels.Check(at));
    }
}
