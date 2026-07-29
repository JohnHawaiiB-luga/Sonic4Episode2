using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class DamageTests
{
    private static string? FindGameRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(directory.FullName, "G_ZONE1", "MAP")))
                return directory.FullName;
            directory = directory.Parent;
        }
        return null;
    }

    private static Player Grounded()
    {
        var grid = new byte[4 + 64 * 4 * 2];
        BinaryPrimitives.WriteUInt16LittleEndian(grid, 64);
        BinaryPrimitives.WriteUInt16LittleEndian(grid.AsSpan(2), 4);
        for (int i = 0; i < 64 * 4; i++) grid[4 + i * 2] = 1;
        var player = new Player(CollisionMap.FromGrid(StageGrid.Parse("t", grid)));
        player.PlaceOnGround(50f, 0f);
        player.Update();
        return player;
    }

    [Fact]
    public void RingedDamageEntersTheRecoveredHurtState()
    {
        var player = Grounded();
        var result = new Damage().Apply(player, 10);

        Assert.Equal(DamageOutcome.Hurt, result.Outcome);
        Assert.Equal(0, result.RingsRemaining);
        Assert.Equal(-0.46875f, player.Velocity.X, precision: 5);
        Assert.Equal(0.9375f, player.Velocity.Y, precision: 5);
        Assert.False(player.OnGround);
        Assert.Equal(180, player.InvincibleTimer);
        Assert.True(player.IsDamaged);
    }

    [Fact]
    public void KnockbackIsOppositeThePlayersFacing()
    {
        var player = Grounded();
        player.InputX = -1f;
        player.Update();
        Assert.True(player.FacingLeft);

        new Damage().Apply(player, 10);

        Assert.Equal(0.46875f, player.Velocity.X, precision: 5);
    }

    [Fact]
    public void InvulnerabilityPreventsRepeatDamage()
    {
        var player = Grounded();
        var damage = new Damage();
        damage.Apply(player, 10);
        player.Velocity = Vector2.Zero;

        var result = damage.Apply(player, 7);

        Assert.Equal(DamageOutcome.Ignored, result.Outcome);
        Assert.Equal(7, result.RingsRemaining);
        Assert.Equal(Vector2.Zero, player.Velocity);
    }

    [Fact]
    public void SuperDoesNotUseTheNormalDamageBranch()
    {
        var player = Grounded();
        player.SetMode(0, Player.SuperMode);

        var result = new Damage().Apply(player, 50);

        Assert.Equal(DamageOutcome.Ignored, result.Outcome);
        Assert.Equal(50, result.RingsRemaining);
        Assert.False(player.IsDamaged);
        Assert.False(player.IsDead);
    }

    [Fact]
    public void DeadPlayersIgnoreFurtherDamage()
    {
        var player = Grounded();
        var damage = new Damage();
        damage.Apply(player, 0);
        player.Velocity = Vector2.Zero;

        var result = damage.Apply(player, 10);

        Assert.Equal(DamageOutcome.Ignored, result.Outcome);
        Assert.Equal(10, result.RingsRemaining);
        Assert.True(player.IsDead);
        Assert.False(player.IsDamaged);
        Assert.Equal(Vector2.Zero, player.Velocity);
    }

    [Fact]
    public void RinglessDamageEntersTheRecoveredDeathState()
    {
        var player = Grounded();

        var result = new Damage().Apply(player, 0);

        Assert.Equal(DamageOutcome.Death, result.Outcome);
        Assert.Equal(0, result.RingsRemaining);
        Assert.True(player.IsDead);
        Assert.Equal(0f, player.Velocity.X);
        Assert.Equal(player.JumpVelocity, player.Velocity.Y);
        Assert.False(player.OnGround);
    }

    [Fact]
    public void HurtStateIgnoresControlUntilLanding()
    {
        var player = Grounded();
        new Damage().Apply(player, 10);
        float knockback = player.Velocity.X;
        player.InputX = 1f;

        player.Update();

        Assert.True(player.IsDamaged);
        Assert.Equal(knockback, player.Velocity.X);
    }

    [Fact]
    public void HurtStateEndsAfterLanding()
    {
        var player = Grounded();
        new Damage().Apply(player, 10);

        for (int i = 0; i < 120 && player.IsDamaged; i++) player.Update();

        Assert.False(player.IsDamaged);
        Assert.True(player.OnGround);
    }

    [Fact]
    public void LandingFromDamageDoesNotAlsoAcceptAJump()
    {
        var player = Grounded();
        new Damage().Apply(player, 10);
        player.InputJump = true;
        for (int i = 0; i < 120 && !player.OnGround; i++) player.Update();
        Assert.True(player.IsDamaged);

        player.Update();

        Assert.False(player.IsDamaged);
        Assert.True(player.OnGround);
        Assert.Equal(0f, player.Velocity.Y);
        player.Update();
        Assert.True(player.OnGround);
        Assert.Equal(0f, player.Velocity.Y);
    }

    [Fact]
    public void DamageClearsActiveMovementSequences()
    {
        var rolling = Grounded();
        rolling.Velocity.X = rolling.RollThreshold * 2f;
        rolling.InputDown = true;
        rolling.Update();
        Assert.True(rolling.Rolling);

        var charging = Grounded();
        charging.InputDown = true;
        charging.Update();
        charging.InputJump = true;
        charging.Update();
        Assert.True(charging.Charging);
        Assert.True(charging.DashPower > 0f);

        var damage = new Damage();
        damage.Apply(rolling, 10);
        damage.Apply(charging, 10);

        Assert.False(rolling.Rolling);
        Assert.False(charging.Charging);
        Assert.Equal(0f, charging.DashPower);
    }

    [Fact]
    public void DeathClearsAnActiveMovementSequence()
    {
        var player = Grounded();
        player.InputDown = true;
        player.Update();
        player.InputJump = true;
        player.Update();
        Assert.True(player.Charging);

        new Damage().Apply(player, 0);

        Assert.False(player.Charging);
        Assert.Equal(0f, player.DashPower);
    }

    [Fact]
    public void DeathStateIgnoresControl()
    {
        var player = Grounded();
        new Damage().Apply(player, 0);
        float x = player.Position.X;
        player.InputX = 1f;

        player.Update();

        Assert.True(player.IsDead);
        Assert.Equal(0f, player.Velocity.X);
        Assert.Equal(x, player.Position.X);
    }

    [Fact]
    public void DeathArcFallsThroughTheStage()
    {
        var player = Grounded();
        float ground = player.Position.Y;
        new Damage().Apply(player, 0);

        for (int i = 0; i < 180; i++) player.Update();

        Assert.True(player.IsDead);
        Assert.False(player.OnGround);
        Assert.True(player.Position.Y < ground - Player.Height);
    }

    [Fact]
    public void EngineDamageDropsThePlayersRealStageRings()
    {
        string? root = FindGameRoot();
        if (root is null) return;

        var engine = new GameEngine(root);
        engine.Step();
        var boxes = engine.ItemBoxes!;
        int ringBox = Enumerable.Range(0, boxes.Count)
            .First(i => boxes.TypeAt(i) == ItemType.Ring10);
        var at = boxes.PositionOf(ringBox);
        engine.Player!.Position = new Vector3(at.X, at.Y, 0f);
        engine.Step();
        Assert.Equal(ItemBoxes.RingsFromMonitor, engine.RingCount);

        DamageResult result = engine.DamagePlayer();

        Assert.Equal(DamageOutcome.Hurt, result.Outcome);
        Assert.Equal(0, engine.RingCount);
        Assert.True(engine.Player.IsDamaged);
    }

    [Fact]
    public void EngineDamageWithoutAPlayerIsIgnored()
    {
        var engine = new GameEngine("");
        DamageResult result = default;

        Exception? error = Record.Exception(() => result = engine.DamagePlayer());

        Assert.Null(error);
        Assert.Equal(DamageOutcome.Ignored, result.Outcome);
        Assert.Equal(0, result.RingsRemaining);
    }
}
