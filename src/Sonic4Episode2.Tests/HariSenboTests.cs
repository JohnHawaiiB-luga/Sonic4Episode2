using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class HariSenboTests
{
    private static string? FindGameRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(
                    directory.FullName, "G_EP1ZONE1", "MAP")))
                return directory.FullName;
            directory = directory.Parent;
        }
        return null;
    }

    private static Player EmptyPlayer()
    {
        var grid = new byte[4 + 64 * 64 * 2];
        BinaryPrimitives.WriteUInt16LittleEndian(grid, 64);
        BinaryPrimitives.WriteUInt16LittleEndian(grid.AsSpan(2), 64);
        return new Player(CollisionMap.FromGrid(StageGrid.Parse("empty", grid)));
    }

    private static HariSenboPlacement Enemy(
        int id,
        int x = 100,
        int y = 100,
        int flags = 0,
        byte width = 0,
        byte height = 0) =>
        new(x, y, id, flags, width, height, 0);

    private static void SetNative(Player player, float x, float y)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        player.Position = new Vector3(x * scale, -y * scale, 0f);
    }

    private static void Advance(HariSenbos enemies, int frames)
    {
        for (int i = 0; i < frames; i++)
            enemies.Step();
    }

    [Fact]
    public void OnlyCatalogHariSenboPlacementsBecomeEnemies()
    {
        var enemies = new HariSenbos(
        [
            Enemy(0),
            Enemy(15),
            Enemy(380),
            Enemy(397),
            Enemy(1),
        ]);

        Assert.Equal(2, enemies.Count);
        Assert.Equal(0, enemies.ObjectIdAt(0));
        Assert.Equal(15, enemies.ObjectIdAt(1));
    }

    [Fact]
    public void RecoveredRectanglesAndWorldPositionArePreserved()
    {
        var enemies = new HariSenbos([Enemy(0, x: 25, y: 40)]);
        float scale = PlayerPhysics.WorldPerPixel;

        Assert.Equal(
            new Vector2(25f * scale, -40f * scale),
            enemies.PositionOf(0));
        Assert.Equal(
            new HariSenboBounds(-12, -12, 12, 12),
            enemies.AttackBoundsAt(0));
        Assert.Equal(
            new HariSenboBounds(-22, -22, 22, 22),
            enemies.DefenseBoundsAt(0));
    }

    [Fact]
    public void RawDimensionsSelectRedWaitAndInflatedDurations()
    {
        var enemies = new HariSenbos(
        [
            Enemy(15, width: 2, height: 1),
            Enemy(15),
        ]);

        Assert.Equal(60, enemies.WaitFramesAt(0));
        Assert.Equal(30, enemies.InflatedFramesAt(0));
        Assert.Equal(300, enemies.WaitFramesAt(1));
        Assert.Equal(300, enemies.InflatedFramesAt(1));
    }

    [Fact]
    public void BlueVariantRemainsStatic()
    {
        var enemies = new HariSenbos([Enemy(0)]);

        Advance(enemies, 1_000);

        Assert.Equal(HariSenboPhase.Static, enemies.PhaseAt(0));
        Assert.False(enemies.IsArmoredAt(0));
        Assert.Equal(
            new HariSenboBounds(-12, -12, 12, 12),
            enemies.AttackBoundsAt(0));
    }

    [Fact]
    public void RedVariantCyclesThroughRecoveredAttackPhases()
    {
        var enemies = new HariSenbos(
            [Enemy(15, width: 2, height: 1)]);

        Assert.Equal(HariSenboPhase.Waiting, enemies.PhaseAt(0));
        Assert.Equal(60, enemies.PhaseFramesRemainingAt(0));

        Advance(enemies, 59);
        Assert.Equal(HariSenboPhase.Waiting, enemies.PhaseAt(0));
        Assert.Equal(1, enemies.PhaseFramesRemainingAt(0));

        enemies.Step();
        Assert.Equal(HariSenboPhase.Windup, enemies.PhaseAt(0));
        Assert.Equal(60, enemies.PhaseFramesRemainingAt(0));
        Assert.False(enemies.IsArmoredAt(0));

        Advance(enemies, 59);
        Assert.Equal(HariSenboPhase.Windup, enemies.PhaseAt(0));
        Assert.Equal(1, enemies.PhaseFramesRemainingAt(0));

        enemies.Step();
        Assert.Equal(HariSenboPhase.Extending, enemies.PhaseAt(0));
        Assert.Equal(15, enemies.PhaseFramesRemainingAt(0));
        Assert.True(enemies.IsArmoredAt(0));
        Assert.Equal(
            new HariSenboBounds(-12, -12, 12, 12),
            enemies.AttackBoundsAt(0));

        Advance(enemies, 14);
        Assert.Equal(HariSenboPhase.Extending, enemies.PhaseAt(0));
        Assert.Equal(1, enemies.PhaseFramesRemainingAt(0));

        enemies.Step();
        Assert.Equal(HariSenboPhase.Inflated, enemies.PhaseAt(0));
        Assert.Equal(30, enemies.PhaseFramesRemainingAt(0));
        Assert.True(enemies.IsArmoredAt(0));
        Assert.Equal(
            new HariSenboBounds(-24, -24, 24, 24),
            enemies.AttackBoundsAt(0));

        Advance(enemies, 29);
        Assert.Equal(HariSenboPhase.Inflated, enemies.PhaseAt(0));
        Assert.Equal(1, enemies.PhaseFramesRemainingAt(0));

        enemies.Step();
        Assert.Equal(HariSenboPhase.Waiting, enemies.PhaseAt(0));
        Assert.Equal(60, enemies.PhaseFramesRemainingAt(0));
        Assert.False(enemies.IsArmoredAt(0));
        Assert.Equal(
            new HariSenboBounds(-12, -12, 12, 12),
            enemies.AttackBoundsAt(0));
    }

    [Fact]
    public void InflatedAttackRectangleReachesFartherThanBase()
    {
        var enemies = new HariSenbos(
            [Enemy(15, width: 1, height: 1)]);
        var player = EmptyPlayer();
        SetNative(player, 119, 100);

        Assert.False(enemies.Check(player));

        Advance(
            enemies,
            enemies.WaitFramesAt(0) +
            HariSenbos.WindupFrames +
            HariSenbos.ExtensionFrames);

        Assert.Equal(HariSenboPhase.Inflated, enemies.PhaseAt(0));
        Assert.True(enemies.Check(player));
    }

    [Fact]
    public void EngineMountsAndDamagesThroughRealLostLabyrinthEnemies()
    {
        string? root = FindGameRoot();
        if (root is null) return;

        var engine = new GameEngine(new FileSystemContent(root))
        {
            ActArchive = "G_EP1ZONE1/MAP/ZONE11_MAP.AMB",
        };
        engine.Step();

        Assert.NotNull(engine.HariSenbos);
        Assert.Equal(3, engine.HariSenbos!.Count);
        Assert.Equal(
            engine.Placements.Count(
                placement => ObjectCatalog.Is(
                    placement.ObjectId,
                    "HariSenbo")),
            engine.HariSenbos.Count);

        int red = Enumerable.Range(0, engine.HariSenbos.Count)
            .Single(i => engine.HariSenbos.ObjectIdAt(i) == 15);
        Assert.Equal(2, engine.HariSenbos.PlacementAt(red).Width);
        Assert.Equal(60, engine.HariSenbos.WaitFramesAt(red));
        Assert.Equal(30, engine.HariSenbos.InflatedFramesAt(red));

        var boxes = engine.ItemBoxes!;
        int ringBox = Enumerable.Range(0, boxes.Count)
            .First(i => boxes.TypeAt(i) == ItemType.Ring10);
        Vector2 boxPosition = boxes.PositionOf(ringBox);
        engine.Player!.Position =
            new Vector3(boxPosition.X, boxPosition.Y, 0f);
        engine.Step();
        Assert.Equal(ItemBoxes.RingsFromMonitor, engine.RingCount);

        Vector2 enemyPosition = engine.HariSenbos.PositionOf(0);
        engine.Player.Position =
            new Vector3(enemyPosition.X, enemyPosition.Y, 0f);
        engine.Player.Velocity = Vector2.Zero;
        Assert.True(engine.HariSenbos.Check(engine.Player));

        engine.Step();

        Assert.Equal(0, engine.RingCount);
        Assert.True(engine.Player.IsDamaged);
    }
}
