using System.Buffers.Binary;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class PlayerModeTests
{
    private static CollisionMap Ground()
    {
        const int width = 64, height = 4;
        var data = new byte[4 + width * height * 2];
        BinaryPrimitives.WriteUInt16LittleEndian(data, width);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(2), height);
        for (int i = 0; i < width * height; i++) data[4 + i * 2] = 1;
        return CollisionMap.FromGrid(StageGrid.Parse("test", data));
    }

    [Fact]
    public void APlayerStartsOnCharacterZeroNormal()
    {
        var player = new Player(Ground());
        Assert.Equal(0, player.Character);
        Assert.Equal(0, player.Mode);
        Assert.False(player.IsSuper);
    }

    [Fact]
    public void SwitchingModeSwitchesTheTuning()
    {
        var player = new Player(Ground());
        float normal = player.MaxSpeed;
        player.SetMode(0, Player.SuperMode);
        Assert.True(player.MaxSpeed > normal);
        Assert.True(player.IsSuper);
    }

    [Fact]
    public void TransformingNeedsEnoughRings()
    {
        var player = new Player(Ground());
        Assert.False(player.TryGoSuper(Player.RingsForSuper - 1));
        Assert.False(player.IsSuper);

        Assert.True(player.TryGoSuper(Player.RingsForSuper));
        Assert.True(player.IsSuper);
    }

    [Fact]
    public void TransformingTwiceDoesNothingTheSecondTime()
    {
        var player = new Player(Ground());
        Assert.True(player.TryGoSuper(200));
        Assert.False(player.TryGoSuper(200));
    }

    [Fact]
    public void RevertingGoesBackToNormalTuning()
    {
        var player = new Player(Ground());
        float normal = player.MaxSpeed;
        player.TryGoSuper(200);
        player.RevertFromSuper();
        Assert.False(player.IsSuper);
        Assert.Equal(normal, player.MaxSpeed);
    }

    [Fact]
    public void SpeedCarriesAcrossATransformation()
    {
        var player = new Player(Ground());
        player.PlaceOnGround(50f, 0f);
        player.InputX = 1f;
        for (int i = 0; i < 100; i++) player.Update();

        float before = player.Velocity.X;
        player.TryGoSuper(200);
        Assert.Equal(before, player.Velocity.X);
    }

    [Fact]
    public void MetalSonicCannotTransform()
    {
        // Character 2's Super row repeats its normal values, so asking it to
        // transform is a no-op rather than something needing a special case.
        var player = new Player(Ground(), PlayerPhysics.For(2, 0));
        player.SetMode(2, 0);
        float normal = player.MaxSpeed;

        player.TryGoSuper(200);
        Assert.Equal(normal, player.MaxSpeed);
    }
}
