using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class WaterAreaTests
{
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

    private static WaterAreaPlacement Area(
        int id,
        int x = 100,
        int y = 100,
        int flags = 0,
        sbyte left = 12,
        sbyte top = 34,
        byte width = 64,
        byte height = 64) =>
        new(x, y, id, flags, left, top, width, height, 0);

    private static void SetNative(Player player, float x, float y)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        player.Position = new Vector3(x * scale, -y * scale, 0f);
    }

    [Fact]
    public void OnlyCatalogWaterAreaPlacementsBecomeAreas()
    {
        var areas = new WaterAreas(
        [
            Area(492),
            Area(132),
            Area(559),
            Area(631),
            Area(9999),
        ]);

        Assert.Equal(1, areas.Count);
        Assert.Equal(492, areas.PlacementAt(0).ObjectId);
    }

    [Theory]
    [InlineData(123, WaterAreaDirection.LeftToRight)]
    [InlineData(124, WaterAreaDirection.RightToLeft)]
    [InlineData(125, WaterAreaDirection.AboveToBelow)]
    [InlineData(126, WaterAreaDirection.BelowToAbove)]
    [InlineData(127, WaterAreaDirection.Immediate)]
    [InlineData(492, WaterAreaDirection.LeftToRight)]
    [InlineData(493, WaterAreaDirection.RightToLeft)]
    [InlineData(494, WaterAreaDirection.AboveToBelow)]
    [InlineData(495, WaterAreaDirection.BelowToAbove)]
    [InlineData(496, WaterAreaDirection.Immediate)]
    public void ObjectIdsSelectRecoveredDirections(
        int id,
        WaterAreaDirection expected)
    {
        var areas = new WaterAreas([Area(id)]);

        Assert.Equal(expected, areas.DirectionAt(0));
    }

    [Fact]
    public void RawFieldsSetLevelDurationAndMinimumRegion()
    {
        var areas = new WaterAreas(
        [
            Area(
                492,
                flags: (1 << 0) | (1 << 2) | (1 << 9) | (1 << 15),
                left: 12,
                top: 34,
                width: 10,
                height: 20),
        ]);

        Assert.Equal(1234, areas.TargetLevelPixelsAt(0));
        Assert.Equal(14 * 60, areas.TransitionFramesAt(0));
        Assert.Equal(
            new WaterAreaBounds(83, 83, 117, 117),
            areas.BoundsPixelsAt(0));
    }

    [Fact]
    public void ImmediateAreaAppliesOnlyNearRestartPosition()
    {
        var near = new WaterAreas(
            [Area(496, x: 100, y: 100, left: 10, top: 0)]);
        var far = new WaterAreas(
            [Area(496, x: 100, y: 100, left: 10, top: 0)]);
        var player = EmptyPlayer();

        SetNative(player, 228, 228);
        Assert.Equal(1, near.Initialize(player));
        Assert.Equal(1000f, near.WaterLevelPixels);

        SetNative(player, 229, 229);
        Assert.Equal(0, far.Initialize(player));
        Assert.Equal(ushort.MaxValue, far.WaterLevelPixels);
    }

    [Fact]
    public void DirectionalAreaRequestsLevelAfterCrossingItsRegion()
    {
        var areas = new WaterAreas(
            [Area(492, left: 12, top: 34)]);
        var player = EmptyPlayer();

        SetNative(player, 80, 100);
        Assert.Equal(0, areas.Step(player));
        Assert.Equal(ushort.MaxValue, areas.WaterLevelPixels);

        SetNative(player, 140, 100);
        Assert.Equal(1, areas.Step(player));
        Assert.Equal(1234f, areas.WaterLevelPixels);
    }

    [Fact]
    public void DirectionalAreaDoesNotFireFromItsDestinationSide()
    {
        var areas = new WaterAreas(
            [Area(492, left: 12, top: 34)]);
        var player = EmptyPlayer();

        SetNative(player, 120, 100);
        Assert.Equal(0, areas.Step(player));

        SetNative(player, 60, 100);
        Assert.Equal(0, areas.Step(player));
        Assert.Equal(ushort.MaxValue, areas.WaterLevelPixels);
    }

    [Fact]
    public void FlagDurationInterpolatesWaterLevelInFrames()
    {
        var areas = new WaterAreas(
        [
            Area(496, x: 0, y: 0, left: 10, top: 0),
            Area(
                492,
                flags: (1 << 0) | (1 << 2),
                left: 12,
                top: 40),
        ]);
        var player = EmptyPlayer();

        SetNative(player, 0, 0);
        Assert.Equal(1, areas.Initialize(player));
        Assert.Equal(1000f, areas.WaterLevelPixels);

        SetNative(player, 80, 100);
        areas.Step(player);
        SetNative(player, 140, 100);
        Assert.Equal(1, areas.Step(player));
        Assert.Equal(1000f, areas.WaterLevelPixels);
        Assert.Equal(1240f, areas.TargetWaterLevelPixels);
        Assert.Equal(240, areas.TransitionFramesRemaining);

        areas.Step(player);
        Assert.Equal(1001f, areas.WaterLevelPixels, 5);
        Assert.Equal(239, areas.TransitionFramesRemaining);
    }

    [Fact]
    public void DirectionalAreaCanRearmDuringAnActiveTransition()
    {
        var areas = new WaterAreas(
        [
            Area(496, x: 0, y: 0, left: 10, top: 0),
            Area(
                492,
                flags: (1 << 0) | (1 << 2),
                left: 12,
                top: 40),
        ]);
        var player = EmptyPlayer();

        SetNative(player, 0, 0);
        areas.Initialize(player);
        SetNative(player, 80, 100);
        areas.Step(player);
        SetNative(player, 140, 100);
        Assert.Equal(1, areas.Step(player));

        SetNative(player, 80, 100);
        Assert.Equal(0, areas.Step(player));
        SetNative(player, 140, 100);
        Assert.Equal(1, areas.Step(player));
        Assert.Equal(240, areas.TransitionFramesRemaining);
    }

    [Fact]
    public void PlayerPhysicsRemainModifiedWhileBelowWaterSurface()
    {
        var areas = new WaterAreas(
            [Area(496, left: 1, top: 0)]);
        var player = EmptyPlayer();
        float normalJump = player.JumpVelocity;
        float normalGravity = player.Gravity;

        SetNative(player, 100, 95);
        areas.Initialize(player);
        areas.Step(player);

        Assert.True(player.IsUnderwater);
        Assert.Equal(
            normalJump * Player.UnderwaterJumpMultiplier,
            player.JumpVelocity,
            5);
        Assert.Equal(
            normalGravity * Player.UnderwaterGravityMultiplier,
            player.Gravity,
            5);

        SetNative(player, 100, 80);
        areas.Step(player);

        Assert.False(player.IsUnderwater);
        Assert.Equal(normalJump, player.JumpVelocity, 5);
        Assert.Equal(normalGravity, player.Gravity, 5);
    }

    [Fact]
    public void EngineMountsEveryRealSylvaniaWaterArea()
    {
        string? root = FindGameRoot();
        if (root is null) return;

        var engine = new GameEngine(new FileSystemContent(root))
        {
            ActArchive = "G_ZONE1/MAP/ZONE11_MAP.AMB",
        };
        engine.Step();

        Assert.NotNull(engine.WaterAreas);
        Assert.Equal(21, engine.WaterAreas!.Count);
        Assert.Equal(
            engine.Placements.Count(
                placement => ObjectCatalog.Is(
                    placement.ObjectId,
                    "WaterArea")),
            engine.WaterAreas.Count);
    }
}
