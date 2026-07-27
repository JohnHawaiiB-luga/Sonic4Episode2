using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;
using Xunit;

namespace Sonic4Episode2.Tests;

/// <summary>
/// Player physics against a hand-built collision map, so the assertions do not
/// depend on any particular stage's contents.
/// </summary>
public class PlayerTests
{
    /// <summary>A floor across the bottom two rows of a 10x10 grid.</summary>
    private static CollisionMap FlatGround()
    {
        var cells = new ushort[10 * 10];
        for (int x = 0; x < 10; x++)
        {
            cells[8 * 10 + x] = 1;
            cells[9 * 10 + x] = 1;
        }
        return Build(cells, 10, 10);
    }

    /// <summary>
    /// Round-trips through the on-disk grid shape rather than adding a
    /// test-only constructor, so the parser is exercised too.
    /// </summary>
    private static CollisionMap Build(ushort[] cells, int width, int height)
    {
        var data = new byte[4 + width * height * 2];
        BitConverter.GetBytes((ushort)width).CopyTo(data, 0);
        BitConverter.GetBytes((ushort)height).CopyTo(data, 2);
        for (int i = 0; i < cells.Length; i++)
            BitConverter.GetBytes(cells[i]).CopyTo(data, 4 + i * 2);
        return CollisionMap.FromGrid(StageGrid.Parse("test", data));
    }

    [Fact]
    public void FallsUnderGravityAndLandsOnGround()
    {
        var player = new Player(FlatGround());
        player.PlaceOnGround(50f, 0f);

        for (int i = 0; i < 60; i++) player.Update();

        Assert.True(player.OnGround);
        Assert.Equal(0f, player.Velocity.Y);
    }

    [Fact]
    public void DoesNotFallThroughTheFloor()
    {
        var player = new Player(FlatGround());
        player.PlaceOnGround(50f, 0f);

        for (int i = 0; i < 300; i++) player.Update();

        // Row 8 begins at world Y -160, so the player must rest at or above it.
        Assert.True(player.Position.Y >= -161f, $"player fell to {player.Position.Y}");
    }

    [Fact]
    public void JumpsFromTheGroundOnly()
    {
        var player = new Player(FlatGround());
        player.PlaceOnGround(50f, 0f);
        for (int i = 0; i < 60; i++) player.Update();
        Assert.True(player.OnGround);

        player.InputJump = true;
        player.Update();

        Assert.True(player.Velocity.Y > 0f);
        Assert.False(player.OnGround);
    }

    [Fact]
    public void HoldingJumpDoesNotBounceOnLanding()
    {
        var player = new Player(FlatGround());
        player.PlaceOnGround(50f, 0f);
        for (int i = 0; i < 60; i++) player.Update();

        // Held from here on: the jump is edge-triggered, so landing must not
        // launch the player again.
        player.InputJump = true;
        player.Update();
        Assert.True(player.Velocity.Y > 0f);

        for (int i = 0; i < 200; i++) player.Update();

        Assert.True(player.OnGround);
        Assert.Equal(0f, player.Velocity.Y);
    }

    [Fact]
    public void AcceleratesUpToTheSpeedLimit()
    {
        var player = new Player(FlatGround());
        player.PlaceOnGround(50f, 0f);
        for (int i = 0; i < 60; i++) player.Update();

        player.InputX = 1f;
        // Episode II accelerates at 0.0354 px/frame, so reaching 9 px/frame takes
        // about 254 frames. The placeholder tuning got there in well under 200.
        for (int i = 0; i < 400; i++) player.Update();

        Assert.True(player.Velocity.X <= player.MaxSpeed + 0.001f);
        Assert.True(player.Velocity.X > player.MaxSpeed - 0.01f);
    }

    [Fact]
    public void FrictionSettlesAtRestWithoutReversing()
    {
        var player = new Player(FlatGround());
        player.PlaceOnGround(50f, 0f);
        for (int i = 0; i < 60; i++) player.Update();

        player.InputX = 1f;
        for (int i = 0; i < 50; i++) player.Update();
        Assert.True(player.Velocity.X > 0f);

        player.InputX = 0f;
        for (int i = 0; i < 200; i++) player.Update();

        // Friction must bleed speed toward zero, never push through it.
        Assert.Equal(0f, player.Velocity.X);
    }

    [Fact]
    public void WallsStopHorizontalMotion()
    {
        var cells = new ushort[10 * 10];
        for (int x = 0; x < 10; x++) { cells[8 * 10 + x] = 1; cells[9 * 10 + x] = 1; }
        for (int y = 0; y < 10; y++) cells[y * 10 + 6] = 1;
        var map = Build(cells, 10, 10);

        var player = new Player(map);
        player.PlaceOnGround(30f, 0f);
        for (int i = 0; i < 60; i++) player.Update();

        player.InputX = 1f;
        for (int i = 0; i < 200; i++) player.Update();

        // Column 6 spans world X 120..140, so the player must stop before it.
        Assert.True(player.Position.X < 120f, $"player reached {player.Position.X}");
    }

    [Fact]
    public void OffGridIsOpenSidewaysAndSolidBelow()
    {
        var map = FlatGround();

        // Running off the side must not hit an invisible wall, but falling out
        // of the world must not be possible either.
        Assert.False(map.IsSolid(-1, 5));
        Assert.False(map.IsSolid(999, 5));
        Assert.True(map.IsSolid(5, 999));
    }

    [Fact]
    public void GroundHeightFindsTheSurfaceBelowAPoint()
    {
        var map = FlatGround();

        // Row 8 is the first solid row, and its top is at world Y -160.
        float? ground = map.GroundHeightAt(50f, 0f, maxCells: 512);
        Assert.NotNull(ground);
        Assert.Equal(-160f, ground!.Value, 3);
    }
}
