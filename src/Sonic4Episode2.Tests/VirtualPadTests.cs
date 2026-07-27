using System.Numerics;
using System.Buffers.Binary;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class VirtualPadTests
{
    private const float W = 1920f, H = 1080f;

    private static VirtualPad Pad() => new(W, H);

    private static Vector2 BottomLeft => new(W * 0.05f, H * 0.9f);
    private static Vector2 BottomRightLow => new(W * 0.95f, H * 0.95f);
    private static Vector2 BottomRightHigh => new(W * 0.95f, H * 0.6f);

    [Fact]
    public void NoTouchesIsNoInput()
    {
        var pad = Pad();
        pad.Update([]);
        Assert.Equal(0f, pad.SteerX);
        Assert.False(pad.Jump);
        Assert.False(pad.Crouch);
    }

    [Fact]
    public void TheLeftOfTheSteerZoneGoesLeftAndTheRightGoesRight()
    {
        var pad = Pad();
        pad.Update([BottomLeft]);
        Assert.Equal(-1f, pad.SteerX);

        pad.Update([new Vector2(W * 0.35f, H * 0.9f)]);
        Assert.Equal(1f, pad.SteerX);
    }

    [Fact]
    public void TheCentreOfTheSteerZoneIsADeadZone()
    {
        var pad = Pad();
        pad.Update([new Vector2(W * VirtualPad.SteerZoneWidth / 2f, H * 0.9f)]);
        Assert.Equal(0f, pad.SteerX);
    }

    [Fact]
    public void TheActionZoneSplitsIntoJumpAboveAndCrouchBelow()
    {
        var pad = Pad();
        pad.Update([BottomRightHigh]);
        Assert.True(pad.Jump);
        Assert.False(pad.Crouch);

        pad.Update([BottomRightLow]);
        Assert.True(pad.Crouch);
        Assert.False(pad.Jump);
    }

    [Fact]
    public void TouchingTheGameAreaControlsNothing()
    {
        var pad = Pad();
        pad.Update([new Vector2(W * 0.05f, H * 0.1f), new Vector2(W * 0.95f, H * 0.2f)]);
        Assert.Equal(0f, pad.SteerX);
        Assert.False(pad.Jump);
        Assert.False(pad.Crouch);
    }

    [Fact]
    public void SteeringAndJumpingAtOnceWorks()
    {
        var pad = Pad();
        pad.Update([BottomLeft, BottomRightHigh]);
        Assert.Equal(-1f, pad.SteerX);
        Assert.True(pad.Jump);
    }

    [Fact]
    public void AThumbOnCrouchAndAnotherOnJumpIsASpinDash()
    {
        // The layout has to allow this specific pair, because it is the whole
        // input for a spin dash.
        var pad = Pad();
        pad.Update([BottomRightLow, BottomRightHigh]);
        Assert.True(pad.Crouch);
        Assert.True(pad.Jump);
    }

    [Fact]
    public void TheLayoutIsProportionalSoItSurvivesAnyScreen()
    {
        var wide = new VirtualPad(2400f, 1080f);
        var tall = new VirtualPad(1080f, 2400f);

        wide.Update([new Vector2(2400f * 0.05f, 1080f * 0.9f)]);
        tall.Update([new Vector2(1080f * 0.05f, 2400f * 0.9f)]);

        Assert.Equal(-1f, wide.SteerX);
        Assert.Equal(-1f, tall.SteerX);
    }

    [Fact]
    public void ItDrivesAPlayer()
    {
        const int width = 64, height = 4;
        var grid = new byte[4 + width * height * 2];
        BinaryPrimitives.WriteUInt16LittleEndian(grid, width);
        BinaryPrimitives.WriteUInt16LittleEndian(grid.AsSpan(2), height);
        for (int i = 0; i < width * height; i++) grid[4 + i * 2] = 1;

        var player = new Player(CollisionMap.FromGrid(StageGrid.Parse("test", grid)));
        player.PlaceOnGround(50f, 0f);

        var pad = Pad();
        pad.Update([new Vector2(W * 0.35f, H * 0.9f)]);
        pad.ApplyTo(player);
        for (int i = 0; i < 60; i++) player.Update();

        Assert.True(player.Velocity.X > 0f);
    }
}
