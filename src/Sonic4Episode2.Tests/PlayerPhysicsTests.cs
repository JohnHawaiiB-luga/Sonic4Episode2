using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class PlayerPhysicsTests
{
    [Fact]
    public void ThreeCharactersOfElevenModes()
    {
        // The shape is the engine's own: `imul ecx, ecx, 0x4a4` scales a character
        // id by 1188 bytes, which is 11 rows of 108.
        Assert.Equal(3, PlayerPhysics.CharacterCount);
        Assert.Equal(11, PlayerPhysics.ModeCount);
        Assert.Equal(33, PlayerPhysics.All.Length);
        Assert.Equal(PlayerPhysics.All[0], PlayerPhysics.For(0, 0));
        Assert.Equal(PlayerPhysics.All[12], PlayerPhysics.For(1, 1));
    }

    [Fact]
    public void CharactersZeroAndOneSharePhysics()
    {
        // 24 of 25 float fields are identical between them; only one slope field
        // differs. Whatever separates those two characters, it is not the tuning.
        var a = PlayerPhysics.For(0, 0);
        var b = PlayerPhysics.For(1, 0);
        Assert.Equal(a.TopSpeed, b.TopSpeed);
        Assert.Equal(a.JumpImpulse, b.JumpImpulse);
        Assert.Equal(a.Gravity, b.Gravity);
    }

    [Fact]
    public void CharacterTwoHasNoSuperMode()
    {
        // Its Super row simply repeats its normal values, which is what you would
        // expect of a character that cannot transform.
        var normal = PlayerPhysics.For(2, 0);
        var super = PlayerPhysics.For(2, 1);
        Assert.Equal(normal.TopSpeed, super.TopSpeed);
        Assert.Equal(normal.JumpImpulse, super.JumpImpulse);

        // Whereas character 0's Super row really is different.
        Assert.NotEqual(PlayerPhysics.For(0, 0).TopSpeed,
                        PlayerPhysics.For(0, 1).TopSpeed);
    }

    [Fact]
    public void ModesSevenAndEightAreHeavilySlowed()
    {
        Assert.True(PlayerPhysics.For(0, 7).TopSpeed < 1f);
        Assert.True(PlayerPhysics.For(0, 8).TopSpeed < 1f);
        // Gravity is untouched, so they are slowed movement rather than slow motion.
        Assert.Equal(PlayerPhysics.For(0, 0).Gravity, PlayerPhysics.For(0, 7).Gravity);
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
        var sonic = PlayerPhysics.For(0, 0);
        var super = PlayerPhysics.For(0, 1);
        Assert.True(super.GroundAcceleration > sonic.GroundAcceleration);
        Assert.True(super.TopSpeed > sonic.TopSpeed);
        Assert.True(super.JumpImpulse > sonic.JumpImpulse);
        // Gravity is the one thing being Super does not change.
        Assert.Equal(sonic.Gravity, super.Gravity);
    }

    [Fact]
    public void MadGearRunsWithALongerCoyoteWindow()
    {
        // The Mad Gear modes are the only ones that move it, 24 frames to 120.
        Assert.Equal(120, PlayerPhysics.For(0, 5).CoyoteFrames);
        Assert.Equal(120, PlayerPhysics.For(0, 6).CoyoteFrames);
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
    public void TheCountersAreTheSameInEveryRow()
    {
        // They are what identified the table, so a row where they drift would
        // mean the stride is wrong.
        Assert.All(PlayerPhysics.All, p =>
        {
            Assert.Equal(1800, p.BreathFrames);
            Assert.Equal(180, p.InvincibleFrames);
            Assert.Equal(96, p.PoolMax);
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
        var super = new Player(map, PlayerPhysics.For(0, 1));
        Assert.True(super.MaxSpeed > new Player(map).MaxSpeed);
    }

    private static Core.Assets.StageGrid TestGrid()
    {
        var data = new byte[4 + 2];
        data[0] = 1; data[2] = 1;
        return Core.Assets.StageGrid.Parse("test", data);
    }
}

public class SlopeTests
{
    /// <summary>
    /// A long strip of solid ground where every cell carries the given `.DI`
    /// angle byte. It has to be long: at top speed the player crosses a cell every
    /// seven frames, and running off the end would go airborne and stop the slope
    /// applying at all.
    /// </summary>
    private static CollisionMap Sloped(byte angleUnits)
    {
        const int width = 512, height = 4;
        var grid = new byte[4 + width * height * 2];
        grid[0] = (byte)(width & 0xFF); grid[1] = (byte)(width >> 8);
        grid[2] = (byte)height;
        for (int i = 0; i < width * height; i++) grid[4 + i * 2] = 1;

        var angles = new byte[4 + CollisionShapes.CellsPerRecord + 4 * 2];
        angles[0] = 4; angles[2] = 1;             // 4 chips, 1 record
        for (int c = 0; c < CollisionShapes.CellsPerRecord; c++)
            angles[4 + c] = angleUnits;

        return CollisionMap.FromGrid(
            StageGrid.Parse("test", grid), null,
            CollisionShapes.Parse(angles, CollisionShapes.CellsPerRecord));
    }

    [Fact]
    public void FlatGroundLeavesSpeedAlone()
    {
        var player = new Player(Sloped(0));
        player.PlaceOnGround(10f, 0f);
        player.Update();
        float before = player.Velocity.X;
        player.Update();
        Assert.Equal(before, player.Velocity.X, precision: 5);
        Assert.Equal(0f, player.GroundAngle);
    }

    [Fact]
    public void GroundRisingToTheRightPushesThePlayerLeft()
    {
        // 32 units of 256 is a positive quarter-of-a-quarter turn: uphill to the
        // right, so a resting player should start sliding back down it.
        var player = new Player(Sloped(224));   // stored byte negates to +45 deg
        player.PlaceOnGround(10f, 0f);
        for (int i = 0; i < 4; i++) player.Update();
        Assert.True(player.GroundAngle > 0f);
        Assert.True(player.Velocity.X < 0f);
    }

    [Fact]
    public void TheOppositeSlopePushesTheOtherWay()
    {
        var player = new Player(Sloped(32));    // -45 deg
        player.PlaceOnGround(10f, 0f);
        for (int i = 0; i < 4; i++) player.Update();
        Assert.True(player.GroundAngle < 0f);
        Assert.True(player.Velocity.X > 0f);
    }

    [Fact]
    public void RunningDownhillCarriesYouPastRunningTopSpeed()
    {
        // The slope cap is 13 px/frame against a running cap of 9, which is the
        // whole reason it is a separate limit.
        var player = new Player(Sloped(32));
        Assert.True(player.SlopeSpeedMax > player.MaxSpeed);

        player.PlaceOnGround(10f, 0f);
        player.InputX = 1f;
        for (int i = 0; i < 900; i++) player.Update();

        Assert.True(player.Velocity.X > player.MaxSpeed);
        Assert.True(player.Velocity.X <= player.SlopeSpeedMax + 0.001f);
    }

    [Fact]
    public void StandingStillOnASlopeDoesNotSlide()
    {
        // Deceleration is 0.125 px/frame and a 45 degree slope contributes
        // 0.0625 * sin(45) = 0.044, so Episode II's tuning holds you in place.
        // This is a fact about the recovered numbers, not a modelling choice.
        var player = new Player(Sloped(32));
        player.PlaceOnGround(10f, 0f);
        for (int i = 0; i < 300; i++) player.Update();
        Assert.True(MathF.Abs(player.Velocity.X) < 0.05f);
    }
}
