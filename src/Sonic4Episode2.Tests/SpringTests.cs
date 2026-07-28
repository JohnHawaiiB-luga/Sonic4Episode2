using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class SpringTests
{
    private static int SpringId { get; } =
        ObjectCatalog.All.First(e => e.Name == "Spring").Id;

    private static Springs Field(params (int X, int Y)[] at) =>
        new(at.Select(p => new Placement(p.X, p.Y, SpringId, 0, 0)).ToList());

    [Fact]
    public void OnlySpringPlacementsBecomeSprings()
    {
        var placements = new List<Placement>
        {
            new(100, 100, SpringId, 0, 0),
            new(200, 100, 9999, 0, 0),          // unknown id
            new(300, 100, 715, 0, 0),           // known id, not a spring
        };
        Assert.Equal(1, new Springs(placements).Count);
    }

    [Fact]
    public void SteppingIntoASpringFires()
    {
        var springs = Field((640, 640));
        var at = springs.PositionOf(0);
        float? impulse = springs.Check(at);
        Assert.NotNull(impulse);
        Assert.Equal(Springs.ImpulsePixels * PlayerPhysics.WorldPerPixel,
                     impulse!.Value, precision: 5);
    }

    [Fact]
    public void StandingInsideDoesNotRefire()
    {
        var springs = Field((640, 640));
        var at = springs.PositionOf(0);
        Assert.NotNull(springs.Check(at));
        Assert.Null(springs.Check(at));           // still inside: no second launch
        Assert.Null(springs.Check(at));
    }

    [Fact]
    public void LeavingAndReturningFiresAgain()
    {
        var springs = Field((640, 640));
        var at = springs.PositionOf(0);
        Assert.NotNull(springs.Check(at));
        Assert.Null(springs.Check(at + new Vector2(1000f, 0f)));   // stepped out
        Assert.NotNull(springs.Check(at));                          // came back
    }

    [Fact]
    public void FarAwayIsNoLaunch()
    {
        var springs = Field((640, 640));
        Assert.Null(springs.Check(new Vector2(0f, 0f)));
    }

    [Fact]
    public void BounceIsNotAJump()
    {
        // A launch cannot be cut short and it uncurls the player.
        var grid = new byte[4 + 64 * 4 * 2];
        BinaryPrimitives.WriteUInt16LittleEndian(grid, 64);
        BinaryPrimitives.WriteUInt16LittleEndian(grid.AsSpan(2), 4);
        for (int i = 0; i < 64 * 4; i++) grid[4 + i * 2] = 1;
        var player = new Player(CollisionMap.FromGrid(StageGrid.Parse("t", grid)));
        player.PlaceOnGround(50f, 0f);
        player.InputX = 1f;
        for (int i = 0; i < 300; i++) player.Update();
        player.InputDown = true;
        player.Update();
        Assert.True(player.Rolling);

        player.Bounce(5f);
        Assert.False(player.Rolling);
        Assert.False(player.OnGround);
        Assert.Equal(5f, player.Velocity.Y);

        // Releasing jump must not halve the rise the way a cut jump would.
        player.InputJump = false;
        float before = player.Velocity.Y;
        player.Update();
        Assert.True(player.Velocity.Y > before - player.Gravity * 1.5f);
    }
}
