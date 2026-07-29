using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class NeedleTests
{
    private static int NeedleId { get; } =
        ObjectCatalog.IdsOfClass("Needle").First();

    private static int ActNeedleId { get; } =
        ObjectCatalog.IdsOfClass("ActNeedle").First();

    private static string? FindGameRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(
                    directory.FullName, "G_ZONE1", "MAP")))
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

    [Fact]
    public void OnlyNeedleClassPlacementsBecomeStaticSpikes()
    {
        var placements = new List<Placement>
        {
            new(100, 100, NeedleId, 0, 0),
            new(200, 100, ActNeedleId, 0, 0),
            new(300, 100, 715, 0, 0),
        };

        Assert.Equal(1, new Needles(placements).Count);
    }

    [Fact]
    public void EpisodeTwoFlagsRotateTheRecoveredBaseVariant()
    {
        var needles = new Needles(
        [
            new Placement(100, 100, 445, 0, 0),
            new Placement(200, 100, 445, 2, 0),
            new Placement(300, 100, 446, 3, 0),
            new Placement(400, 100, 447, 0, 0),
            new Placement(500, 100, 448, 0, 0),
        ]);

        Assert.Equal(NeedleDirection.Up, needles.DirectionAt(0));
        Assert.Equal(NeedleDirection.Down, needles.DirectionAt(1));
        Assert.Equal(NeedleDirection.Up, needles.DirectionAt(2));
        Assert.Equal(NeedleDirection.Down, needles.DirectionAt(3));
        Assert.Equal(NeedleDirection.Right, needles.DirectionAt(4));
    }

    [Fact]
    public void EpisodeMetalIdsKeepTheirRecoveredDirections()
    {
        var needles = new Needles(
        [
            new Placement(100, 100, 84, 0, 0),
            new Placement(200, 100, 85, 0, 0),
            new Placement(300, 100, 86, 0, 0),
            new Placement(400, 100, 87, 0, 0),
        ]);

        Assert.Equal(NeedleDirection.Up, needles.DirectionAt(0));
        Assert.Equal(NeedleDirection.Left, needles.DirectionAt(1));
        Assert.Equal(NeedleDirection.Down, needles.DirectionAt(2));
        Assert.Equal(NeedleDirection.Right, needles.DirectionAt(3));
    }

    [Fact]
    public void FallingOntoAnUpwardNeedleReachesItsRecoveredSolidAndAttack()
    {
        var needles = new Needles([new Placement(640, 640, 445, 0, 0)]);
        Vector2 at = needles.PositionOf(0);
        float scale = PlayerPhysics.WorldPerPixel;
        var player = EmptyPlayer();
        player.Position = new Vector3(at.X, at.Y + 31f * scale, 0f);
        player.Velocity = new Vector2(0f, -2f * scale);

        bool touchesAttack = needles.Check(player);

        Assert.True(touchesAttack);
        Assert.Equal(at.Y + 32f * scale, player.Position.Y, precision: 5);
        Assert.Equal(0f, player.Velocity.Y);
        Assert.True(player.OnGround);
    }

    [Fact]
    public void UpwardNeedleAttackIsDisabledUntilThePlayerRidesIt()
    {
        var needles = new Needles([new Placement(640, 640, 445, 0, 0)]);
        Vector2 at = needles.PositionOf(0);
        float scale = PlayerPhysics.WorldPerPixel;
        var player = EmptyPlayer();
        player.Position = new Vector3(at.X, at.Y + 10f * scale, 0f);

        Assert.False(needles.Check(player));
    }

    [Fact]
    public void EpisodeTwoUpwardNeedleHasItsRecoveredWiderTop()
    {
        var episodeTwo = new Needles(
            [new Placement(640, 640, 445, 0, 0)]);
        var episodeMetal = new Needles(
            [new Placement(640, 640, 84, 0, 0)]);
        Vector2 at = episodeTwo.PositionOf(0);
        float scale = PlayerPhysics.WorldPerPixel;
        var episodeTwoPlayer = EmptyPlayer();
        episodeTwoPlayer.Position = new Vector3(
            at.X - 20f * scale,
            at.Y + 31f * scale,
            0f);
        episodeTwoPlayer.Velocity = new Vector2(0f, -2f * scale);
        var episodeMetalPlayer = EmptyPlayer();
        episodeMetalPlayer.Position = episodeTwoPlayer.Position;
        episodeMetalPlayer.Velocity = episodeTwoPlayer.Velocity;

        Assert.True(episodeTwo.Check(episodeTwoPlayer));
        Assert.False(episodeMetal.Check(episodeMetalPlayer));
    }

    [Fact]
    public void UpwardNeedleIsSolidButSafeFromItsSide()
    {
        var needles = new Needles([new Placement(640, 640, 445, 0, 0)]);
        Vector2 at = needles.PositionOf(0);
        float scale = PlayerPhysics.WorldPerPixel;
        var player = EmptyPlayer();
        player.Position = new Vector3(
            at.X - 20f * scale,
            at.Y + 10f * scale,
            0f);
        player.Velocity = new Vector2(4f * scale, 0f);

        bool touchesAttack = needles.Check(player);

        Assert.False(touchesAttack);
        Assert.Equal(at.X - 22f * scale, player.Position.X, precision: 5);
        Assert.Equal(0f, player.Velocity.X);
    }

    [Fact]
    public void DownwardNeedleCanBeStoodOnFromItsFlatSide()
    {
        var needles = new Needles([new Placement(640, 640, 86, 0, 0)]);
        Vector2 at = needles.PositionOf(0);
        float scale = PlayerPhysics.WorldPerPixel;
        var player = EmptyPlayer();
        player.Position = new Vector3(
            at.X,
            at.Y - scale,
            0f);
        player.Velocity = new Vector2(0f, -4f * scale);

        bool touchesAttack = needles.Check(player);

        Assert.False(touchesAttack);
        Assert.Equal(at.Y, player.Position.Y, precision: 5);
        Assert.Equal(0f, player.Velocity.Y);
        Assert.True(player.OnGround);
    }

    [Theory]
    [InlineData(85, -20f, -4f)]
    [InlineData(86, 0f, -30f)]
    [InlineData(87, 20f, -4f)]
    public void SideAndDownNeedlesUseTheirRecoveredAttackRectangles(
        int objectId, float playerOffsetX, float playerOffsetY)
    {
        var needles = new Needles([new Placement(640, 640, objectId, 0, 0)]);
        Vector2 at = needles.PositionOf(0);
        float scale = PlayerPhysics.WorldPerPixel;
        var player = EmptyPlayer();
        player.Position = new Vector3(
            at.X + playerOffsetX * scale,
            at.Y + playerOffsetY * scale,
            0f);

        Assert.True(needles.Check(player));
    }

    [Theory]
    [InlineData(85, -40f, -4f, 4f, 0f, -42f, -4f)]
    [InlineData(86, 0f, -56f, 0f, 4f, 0f, -57f)]
    [InlineData(87, 40f, -4f, -4f, 0f, 42f, -4f)]
    public void SolidFacesStopCrossingBeforeTheNeedleAttacks(
        int objectId,
        float playerOffsetX,
        float playerOffsetY,
        float velocityX,
        float velocityY,
        float expectedOffsetX,
        float expectedOffsetY)
    {
        var needles = new Needles([new Placement(640, 640, objectId, 0, 0)]);
        Vector2 at = needles.PositionOf(0);
        float scale = PlayerPhysics.WorldPerPixel;
        var player = EmptyPlayer();
        player.Position = new Vector3(
            at.X + playerOffsetX * scale,
            at.Y + playerOffsetY * scale,
            0f);
        player.Velocity = new Vector2(velocityX * scale, velocityY * scale);

        bool touchesAttack = needles.Check(player);

        Assert.True(touchesAttack);
        Assert.Equal(
            at.X + expectedOffsetX * scale,
            player.Position.X,
            precision: 5);
        Assert.Equal(
            at.Y + expectedOffsetY * scale,
            player.Position.Y,
            precision: 5);
        Assert.Equal(0f, player.Velocity.X);
        Assert.Equal(0f, player.Velocity.Y);
    }

    [Fact]
    public void MountedNeedlesDamageTheStagePlayerThroughTheScheduler()
    {
        string? root = FindGameRoot();
        if (root is null) return;

        var engine = new GameEngine(root);
        engine.Step();

        Assert.NotNull(engine.Needles);
        Assert.Equal(
            engine.Placements.Count(
                placement => ObjectCatalog.Is(placement.ObjectId, "Needle")),
            engine.Needles!.Count);
        Assert.True(engine.Needles.Count > 0);

        var boxes = engine.ItemBoxes!;
        int ringBox = Enumerable.Range(0, boxes.Count)
            .First(i => boxes.TypeAt(i) == ItemType.Ring10);
        Vector2 boxPosition = boxes.PositionOf(ringBox);
        engine.Player!.Position = new Vector3(
            boxPosition.X, boxPosition.Y, 0f);
        engine.Step();
        Assert.Equal(ItemBoxes.RingsFromMonitor, engine.RingCount);

        int upward = Enumerable.Range(0, engine.Needles.Count)
            .First(i => engine.Needles.DirectionAt(i) == NeedleDirection.Up);
        Vector2 needlePosition = engine.Needles.PositionOf(upward);
        engine.Player.Position = new Vector3(
            needlePosition.X,
            needlePosition.Y + 32f * PlayerPhysics.WorldPerPixel,
            0f);
        engine.Player.Velocity = Vector2.Zero;

        engine.Step();

        Assert.Equal(0, engine.RingCount);
        Assert.True(engine.Player.IsDamaged);
    }
}
