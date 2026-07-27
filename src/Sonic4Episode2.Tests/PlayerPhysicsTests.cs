using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class PlayerPhysicsTests
{
    [Fact]
    public void SevenModes_MatchingEpisodeOnesCharacterTable()
    {
        Assert.Equal(7, PlayerPhysics.All.Length);
    }

    [Fact]
    public void SonicsRowIsTheOneReadFromTheBinary()
    {
        var s = PlayerPhysics.Sonic;
        Assert.Equal(0.0354004f, s.GroundAcceleration, precision: 6);
        Assert.Equal(9.0f, s.TopSpeed);
        Assert.Equal(0.125f, s.GroundDeceleration);
        Assert.Equal(5.64697265625f, s.JumpImpulse);
        Assert.Equal(0.166015625f, s.Gravity);
        Assert.Equal(15.0f, s.TerminalVelocity);
        Assert.Equal(0.0625f, s.AirAcceleration);
        Assert.Equal(9.0f, s.AirSpeedMax);
        Assert.Equal(0.0625f, s.AirDrag);
    }

    [Fact]
    public void TheIntegerCountersAreEpisodeOnesUntouched()
    {
        // These four are what confirm the table is what it looks like: no float
        // search would have found them, and Episode II left them alone.
        var s = PlayerPhysics.Sonic;
        Assert.Equal(1800, s.BreathFrames);      // 30 s of air
        Assert.Equal(180, s.InvincibleFrames);   // 3 s after a hit
        Assert.Equal(96, s.PoolMax);
        Assert.Equal(24, s.CoyoteFrames);
    }

    [Fact]
    public void SuperSonicIsFasterOnEveryAxisThatMatters()
    {
        var sonic = PlayerPhysics.All[0];
        var super = PlayerPhysics.All[1];
        Assert.True(super.GroundAcceleration > sonic.GroundAcceleration);
        Assert.True(super.TopSpeed > sonic.TopSpeed);
        Assert.True(super.JumpImpulse > sonic.JumpImpulse);
        // Gravity is the one thing being Super does not change.
        Assert.Equal(sonic.Gravity, super.Gravity);
    }

    [Fact]
    public void MadGearRunsWithALongerCoyoteWindow()
    {
        // The Mad Gear rows are the only ones that move it, 24 frames to 120.
        Assert.Equal(120, PlayerPhysics.All[5].CoyoteFrames);
        Assert.Equal(120, PlayerPhysics.All[6].CoyoteFrames);
    }

    [Fact]
    public void EveryRowIsSelfConsistent()
    {
        Assert.All(PlayerPhysics.All, p =>
        {
            Assert.True(p.TopSpeed <= p.TerminalVelocity);
            Assert.True(p.GroundAcceleration > 0f);
            Assert.True(p.Gravity > 0f);
            Assert.True(p.JumpImpulse > p.Gravity);
        });
    }

    [Fact]
    public void GamePixelsConvertThroughTheCollisionCell()
    {
        // A collision cell is 64 game pixels and 20 world units.
        Assert.Equal(64f, PlayerPhysics.PixelsPerCell);
        Assert.Equal(60, PlayerPhysics.FrameRate);
        Assert.Equal(20f / 64f, PlayerPhysics.WorldPerPixel, precision: 6);
    }

    [Fact]
    public void ThePlayerRunsOnSonicsRowByDefault()
    {
        var map = CollisionMap.FromGrid(TestGrid());
        var player = new Player(map);
        Assert.Equal(PlayerPhysics.Sonic, player.Physics);
        Assert.Equal(9.0f * PlayerPhysics.WorldPerPixel, player.MaxSpeed, precision: 5);
    }

    [Fact]
    public void ADifferentRowGivesADifferentPlayer()
    {
        var map = CollisionMap.FromGrid(TestGrid());
        var super = new Player(map, PlayerPhysics.All[1]);
        Assert.True(super.MaxSpeed > new Player(map).MaxSpeed);
    }

    private static Core.Assets.StageGrid TestGrid()
    {
        var data = new byte[4 + 2];
        data[0] = 1; data[2] = 1;
        return Core.Assets.StageGrid.Parse("test", data);
    }
}
