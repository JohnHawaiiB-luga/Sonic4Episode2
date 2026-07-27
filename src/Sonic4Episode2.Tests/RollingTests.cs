using System.Buffers.Binary;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class RollingTests
{
    private static CollisionMap Ground()
    {
        const int width = 512, height = 4;
        var data = new byte[4 + width * height * 2];
        BinaryPrimitives.WriteUInt16LittleEndian(data, width);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(2), height);
        for (int i = 0; i < width * height; i++) data[4 + i * 2] = 1;
        return CollisionMap.FromGrid(StageGrid.Parse("test", data));
    }

    private static Player Running(float frames = 400f)
    {
        var player = new Player(Ground());
        player.PlaceOnGround(50f, 0f);
        player.InputX = 1f;
        for (int i = 0; i < frames; i++) player.Update();
        return player;
    }

    [Fact]
    public void CrouchingAtSpeedStartsARoll()
    {
        var player = Running();
        Assert.False(player.Rolling);
        player.InputDown = true;
        player.Update();
        Assert.True(player.Rolling);
    }

    [Fact]
    public void CrouchingAtAStandstillDoesNot()
    {
        var player = new Player(Ground());
        player.PlaceOnGround(50f, 0f);
        player.Update();
        player.InputDown = true;
        player.Update();
        Assert.False(player.Rolling);
    }

    [Fact]
    public void ARollCoastsFurtherThanRunningWouldWithoutInput()
    {
        var rolled = Running();
        rolled.InputDown = true;
        rolled.Update();
        rolled.InputX = 0f;

        var coasted = Running();
        coasted.InputX = 0f;

        for (int i = 0; i < 30; i++) { rolled.Update(); coasted.Update(); }

        // Rolling friction is 0.03125 against a running 0.125, so the curled one
        // must still be carrying more speed.
        Assert.True(rolled.Rolling);
        Assert.True(rolled.Velocity.X > coasted.Velocity.X);
    }

    [Fact]
    public void SteeringIntoARollExtendsItAndSteeringAgainstItKillsIt()
    {
        var with = Running();
        with.InputDown = true; with.Update(); with.InputX = 1f;

        var against = Running();
        against.InputDown = true; against.Update(); against.InputX = -1f;

        for (int i = 0; i < 60; i++) { with.Update(); against.Update(); }
        Assert.True(with.Velocity.X > against.Velocity.X);
    }

    [Fact]
    public void ARollEndsWhenItRunsOutOfSpeed()
    {
        var player = Running();
        player.InputDown = true;
        player.Update();
        player.InputX = -1f;                 // steer against it to bleed speed off
        for (int i = 0; i < 4000; i++) player.Update();
        Assert.False(player.Rolling);
    }

    [Fact]
    public void JumpingUncurls()
    {
        var player = Running();
        player.InputDown = true;
        player.Update();
        Assert.True(player.Rolling);

        player.InputJump = true;
        player.Update();
        Assert.False(player.Rolling);
    }

    [Fact]
    public void RollingCannotBeSteeredToGoFaster()
    {
        var player = Running();
        player.InputDown = true;
        player.Update();
        float atStart = player.Velocity.X;
        for (int i = 0; i < 20; i++) player.Update();
        Assert.True(player.Velocity.X <= atStart);
    }

    [Fact]
    public void RollingUsesTheStrongerSlopeFactor()
    {
        var player = new Player(Ground());
        Assert.True(player.SlopeFactorRolling > player.SlopeFactor);
        Assert.Equal(0.15625f * PlayerPhysics.WorldPerPixel,
                     player.SlopeFactorRolling, precision: 6);
        Assert.Equal(0.03125f * PlayerPhysics.WorldPerPixel,
                     player.RollFriction, precision: 6);
    }
}
