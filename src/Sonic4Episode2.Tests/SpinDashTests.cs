using System.Buffers.Binary;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class SpinDashTests
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

    private static Player Standing()
    {
        var player = new Player(Ground());
        player.PlaceOnGround(50f, 0f);
        player.Update();
        return player;
    }

    /// <summary>The frame the button goes down. Charge lands on this frame.</summary>
    private static void Press(Player player)
    {
        player.InputJump = true;
        player.Update();
    }

    /// <summary>
    /// A full tap. The release frame is needed because jump is edge-triggered:
    /// without it the next press is not a fresh one and adds no charge.
    /// </summary>
    private static void Tap(Player player)
    {
        Press(player);
        player.InputJump = false;
        player.Update();
    }

    [Fact]
    public void TheRecoveredLaunchFormulaIsEpisodeTwos()
    {
        Assert.Equal(8f, PlayerPhysics.SpinDashLaunchBase);
        Assert.Equal(0.5f, PlayerPhysics.SpinDashLaunchPerCharge);
        Assert.Equal(0.03125f, PlayerPhysics.SpinDashDecayRate);

        // One charge launches at 9.5 px/frame and a full one at 13.0 - a far
        // wider span than Episode I's 12.125 to 13.0.
        var sonic = PlayerPhysics.Sonic;
        Assert.Equal(9.5f, PlayerPhysics.SpinDashLaunchBase +
                           sonic.SpinDashBase * PlayerPhysics.SpinDashLaunchPerCharge);
        Assert.Equal(13.0f, PlayerPhysics.SpinDashLaunchBase +
                            sonic.SpinDashMax * PlayerPhysics.SpinDashLaunchPerCharge);
    }

    [Fact]
    public void CrouchingAtAStandstillStartsAWindUp()
    {
        var player = Standing();
        player.InputDown = true;
        player.Update();
        Assert.True(player.Charging);
        Assert.Equal(0f, player.DashPower);
    }

    [Fact]
    public void TheFirstPressSetsTheBaseChargeAndFurtherPressesAddToIt()
    {
        var player = Standing();
        player.InputDown = true;
        player.Update();

        Press(player);
        Assert.Equal(player.SpinDashBase, player.DashPower, precision: 5);

        player.InputJump = false;
        player.Update();
        float first = player.DashPower;

        Press(player);
        Assert.True(player.DashPower > first);
    }

    [Fact]
    public void ChargeIsCappedNoMatterHowManyPresses()
    {
        var player = Standing();
        player.InputDown = true;
        player.Update();
        for (int i = 0; i < 40; i++) Tap(player);
        Assert.True(player.DashPower <= player.SpinDashMax + 1e-4f);
    }

    [Fact]
    public void HesitatingBleedsTheChargeAway()
    {
        var player = Standing();
        player.InputDown = true;
        player.Update();
        Tap(player);
        float charged = player.DashPower;

        for (int i = 0; i < 30; i++) player.Update();
        Assert.True(player.DashPower < charged);
        Assert.True(player.DashPower > 0f);
    }

    [Fact]
    public void ReleasingCrouchLaunchesAndCurls()
    {
        var player = Standing();
        player.InputDown = true;
        player.Update();
        Press(player);

        float expected = PlayerPhysics.SpinDashLaunchBase * PlayerPhysics.WorldPerPixel +
                         player.DashPower * PlayerPhysics.SpinDashLaunchPerCharge;

        player.InputDown = false;
        player.Update();

        Assert.False(player.Charging);
        Assert.True(player.Rolling);
        Assert.Equal(expected, player.Velocity.X, precision: 4);
    }

    [Fact]
    public void AFullChargeLaunchesFasterThanASingleOne()
    {
        var weak = Standing();
        weak.InputDown = true; weak.Update();
        Tap(weak);
        weak.InputDown = false; weak.Update();

        var strong = Standing();
        strong.InputDown = true; strong.Update();
        for (int i = 0; i < 8; i++) Tap(strong);
        strong.InputDown = false; strong.Update();

        Assert.True(strong.Velocity.X > weak.Velocity.X);
        // And a spin dash beats simply running, which is the point of it.
        Assert.True(weak.Velocity.X > weak.MaxSpeed);
    }

    [Fact]
    public void ChargingDoesNotAlsoJump()
    {
        var player = Standing();
        player.InputDown = true;
        player.Update();
        Press(player);
        Assert.True(player.OnGround);
        Assert.Equal(0f, player.Velocity.Y);
    }

    [Fact]
    public void LaunchingLeftGoesLeft()
    {
        var player = new Player(Ground());
        player.PlaceOnGround(200f, 0f);
        player.InputX = -1f;
        for (int i = 0; i < 30; i++) player.Update();
        player.InputX = 0f;
        for (int i = 0; i < 400; i++) player.Update();   // come to a stop

        player.InputDown = true;
        player.Update();
        Tap(player);
        player.InputDown = false;
        player.Update();

        Assert.True(player.Velocity.X < 0f);
    }
}
