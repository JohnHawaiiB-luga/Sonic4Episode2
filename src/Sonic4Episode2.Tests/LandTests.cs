using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class LandTests
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

    private static LandPlacement Land(
        int id = 534,
        int flags = 0,
        int x = 100,
        int y = 100,
        sbyte left = 0,
        sbyte top = 0,
        byte width = 0,
        byte height = 0) =>
        new(x, y, id, flags, left, top, width, height, 0);

    [Fact]
    public void EventReaderPreservesPathFieldsAndMatchesOnlyCatalogClass()
    {
        byte[] data = EventData(
            (534, 0x1234, -12, 7, 40, 20, 9),
            (541, 0, 2, 3, 0, 0, 0),
            (445, 0, -1, -2, 8, 9, 0));

        var lands = Lands.FromEventData(data);

        Assert.Equal(1, lands.Count);
        Assert.Equal(
            new LandPlacement(10, 20, 534, 0x1234, -12, 7, 40, 20, 9),
            lands.PlacementAt(0));
    }

    [Theory]
    [InlineData(0, 4)]
    [InlineData(1, 2)]
    [InlineData(2, 3)]
    [InlineData(3, 5)]
    public void LowFlagBitsSelectRecoveredSinusoidSpeed(int flags, int speed)
    {
        var lands = new Lands(
            [Land(flags: flags, left: -40, width: 40)]);
        float scale = PlayerPhysics.WorldPerPixel;

        lands.Step(1);

        float expectedPixels =
            80f + 20f *
            MathF.Sin(2f * MathF.PI * (256 + speed) / 1024f);
        Assert.Equal(expectedPixels * scale, lands.PositionOf(0).X, 4);
    }

    [Fact]
    public void DirectionFlagRunsTheHorizontalSinusoidInOppositePhase()
    {
        var lands = new Lands(
            [Land(flags: 8, left: -40, top: -40, width: 40, height: 40)]);
        float scale = PlayerPhysics.WorldPerPixel;

        lands.Step(0);

        Assert.Equal(60f * scale, lands.PositionOf(0).X, 4);
        Assert.Equal(-100f * scale, lands.PositionOf(0).Y, 4);
    }

    [Fact]
    public void WaitFlagStartsTheSinusoidOnlyAfterThePlatformIsRidden()
    {
        var lands = new Lands(
            [Land(flags: 4, left: -20, width: 40)]);
        var player = EmptyPlayer();
        float scale = PlayerPhysics.WorldPerPixel;
        player.Position = new Vector3(
            100f * scale,
            (-100f + 20f) * scale,
            0f);

        lands.Step(100, player);
        Vector2 attached = lands.PositionOf(0);
        lands.Step(101, player);
        Vector2 firstActiveFrame = lands.PositionOf(0);
        lands.Step(102, player);

        Assert.Equal(attached, firstActiveFrame);
        Assert.True(lands.PositionOf(0).X > firstActiveFrame.X);
    }

    [Theory]
    [InlineData(98)]
    [InlineData(537)]
    public void RectanglePlatformTraversesItsAuthoredPerimeter(int objectId)
    {
        var lands = new Lands(
            [Land(id: objectId, left: -5, top: 0, width: 5, height: 5)]);
        float scale = PlayerPhysics.WorldPerPixel;

        lands.Step(256);

        Assert.Equal(100f * scale, lands.PositionOf(0).X, 4);
        Assert.Equal(-110f * scale, lands.PositionOf(0).Y, 4);
    }

    [Fact]
    public void RoutePlatformUsesRoutePointClassAndHalfTopSpeed()
    {
        var lands = new Lands(
        [
            Land(id: 541, x: 100, y: 100, left: 1, top: 0),
            Land(id: 541, x: 110, y: 100, left: 1, top: 1),
            Land(id: 540, x: 100, y: 100, left: 1, top: 4),
        ]);
        float scale = PlayerPhysics.WorldPerPixel;

        lands.Step(0);
        lands.Step(1);

        Assert.Equal(LandMotion.Route, lands.MotionAt(0));
        Assert.Equal(102f * scale, lands.PositionOf(0).X, 4);
        Assert.Equal(-100f * scale, lands.PositionOf(0).Y, 4);
    }

    [Fact]
    public void RoutePlatformWithZeroSpeedRemainsAtItsCurrentPoint()
    {
        var lands = new Lands(
        [
            Land(id: 541, x: 100, y: 100, left: 1, top: 0),
            Land(id: 541, x: 110, y: 100, left: 1, top: 1),
            Land(id: 540, x: 100, y: 100, left: 1, top: 0),
        ]);
        float scale = PlayerPhysics.WorldPerPixel;

        lands.Step(0);
        lands.Step(1);

        Assert.Equal(100f * scale, lands.PositionOf(0).X, 4);
        Assert.Equal(-100f * scale, lands.PositionOf(0).Y, 4);
    }

    [Fact]
    public void RouteEndpointFlagStopsInsteadOfPingPonging()
    {
        LandPlacement[] route =
        [
            Land(id: 541, x: 100, y: 100, left: 1, top: 0),
            Land(id: 541, x: 110, y: 100, left: 1, top: 1),
        ];
        var pingPong = new Lands(
            [.. route, Land(id: 540, x: 100, y: 100, flags: 4, left: 1, top: 4)]);
        var stop = new Lands(
            [.. route, Land(id: 540, x: 100, y: 100, flags: 5, left: 1, top: 4)]);
        float scale = PlayerPhysics.WorldPerPixel;

        for (ulong frame = 0; frame <= 6; frame++)
        {
            pingPong.Step(frame);
            stop.Step(frame);
        }

        Assert.Equal(108f * scale, pingPong.PositionOf(0).X, 4);
        Assert.Equal(110f * scale, stop.PositionOf(0).X, 4);
    }

    [Fact]
    public void RecoveredCollisionFamiliesSelectTheirPlatformBoxes()
    {
        var normal = new Lands([Land(id: 534)]);
        var wide = new Lands([Land(id: 535)]);
        var large = new Lands([Land(id: 536)], "G_ZONE3/MAP/ZONE32B_MAP.AMB");
        var final = new Lands([Land(id: 536)], "G_ZONEF/MAP/ZONEF1_MAP.AMB");
        var typeThreeZoneTwo = new Lands(
            [Land(id: 538)],
            "G_ZONE2/MAP/ZONE22B_MAP.AMB");
        var typeThreeZoneFour = new Lands(
            [Land(id: 538)],
            "G_ZONE4/MAP/ZONE42B_MAP.AMB");
        var laterFamily = new Lands(
            [Land(id: 81)],
            "G_EP1ZONE4/MAP/ZONE41_MAP.AMB");
        var specialTypeTwo = new Lands(
            [Land(id: 83)],
            "G_EP1ZONE3/MAP/CUTSCENE06_MAP.AMB");

        Assert.Equal(new LandCollisionBox(56, 8, -28, -20, true),
                     normal.CollisionAt(0));
        Assert.Equal(new LandCollisionBox(88, 8, -44, -20, true),
                     wide.CollisionAt(0));
        Assert.Equal(new LandCollisionBox(64, 64, -32, -31, false),
                     large.CollisionAt(0));
        Assert.Equal(new LandCollisionBox(64, 64, -32, -31, false),
                     final.CollisionAt(0));
        Assert.Equal(new LandCollisionBox(64, 8, -32, -20, true),
                     typeThreeZoneTwo.CollisionAt(0));
        Assert.Equal(new LandCollisionBox(56, 8, -28, -16, true),
                     typeThreeZoneFour.CollisionAt(0));
        Assert.Equal(new LandCollisionBox(48, 8, -24, -16, true),
                     laterFamily.CollisionAt(0));
        Assert.Equal(new LandCollisionBox(24, 32, -12, -15, false),
                     specialTypeTwo.CollisionAt(0));
    }

    [Fact]
    public void LandingResolvesToTheRecoveredOneWayTop()
    {
        var lands = new Lands([Land()]);
        var player = EmptyPlayer();
        float scale = PlayerPhysics.WorldPerPixel;
        float platformTop = (-100f + 20f) * scale;
        player.Position = new Vector3(100f * scale, platformTop - scale, 0f);
        player.Velocity = new Vector2(0f, -2f * scale);

        lands.Step(0, player);

        Assert.Equal(platformTop, player.Position.Y, 4);
        Assert.Equal(0f, player.Velocity.Y);
        Assert.True(player.OnGround);
    }

    [Fact]
    public void RidingUsesTempOffsetWithoutWritingPlatformTravelIntoPosition()
    {
        var lands = new Lands(
            [Land(left: -40, width: 40)]);
        var player = EmptyPlayer();
        float scale = PlayerPhysics.WorldPerPixel;
        player.Position = new Vector3(
            100f * scale,
            (-100f + 20f) * scale,
            0f);

        lands.Step(0, player);
        Vector3 attachedPosition = player.Position;
        lands.Step(64, player);

        Assert.Equal(attachedPosition, player.Position);
        Assert.Equal(-20f * scale, player.TempOffset.X, 4);
        Assert.Equal(0f, player.TempOffset.Y);

        ulong frame = 64;
        player.OnCollide = instance =>
            lands.Step(frame, (Player)instance);
        player.Update();
        Assert.Equal(attachedPosition.X - 20f * scale, player.Position.X, 4);

        frame = 128;
        player.Update();
        Assert.Equal(attachedPosition.X - 40f * scale, player.Position.X, 4);
    }

    [Fact]
    public void RiderTriggeredPlatformWaitsThirtyFramesThenFallsAtRecoveredRate()
    {
        var lands = new Lands([Land(flags: 64)]);
        var player = EmptyPlayer();
        float scale = PlayerPhysics.WorldPerPixel;
        player.Position = new Vector3(
            100f * scale,
            (-100f + 20f) * scale,
            0f);

        for (ulong frame = 0; frame < 30; frame++)
            lands.Step(frame, player);

        Assert.True(lands.IsFallingAt(0));
        float before = lands.PositionOf(0).Y;

        lands.Step(30);

        Assert.Equal(
            before - 0.1640625f * scale,
            lands.PositionOf(0).Y,
            4);

        for (ulong frame = 31; frame < 100; frame++)
            lands.Step(frame);
        Assert.Equal(7.5f, lands.FallSpeedPixelsAt(0), 5);
    }

    [Fact]
    public void EngineMountsEveryRealLandPlacementWithItsRawPathData()
    {
        string? root = FindGameRoot();
        if (root is null) return;

        var engine = new GameEngine(root);
        engine.Step();

        Assert.NotNull(engine.Lands);
        Assert.Equal(
            engine.Placements.Count(
                placement => ObjectCatalog.Is(placement.ObjectId, "Land")),
            engine.Lands!.Count);
        Assert.True(engine.Lands.Count > 0);
        Assert.Contains(
            Enumerable.Range(0, engine.Lands.Count),
            index =>
            {
                LandPlacement placement = engine.Lands.PlacementAt(index);
                return placement.Width != 0 || placement.Height != 0;
            });

        Vector2[] before = Enumerable.Range(0, engine.Lands.Count)
            .Select(engine.Lands.PositionOf)
            .ToArray();
        engine.Step();

        Assert.Contains(
            Enumerable.Range(0, engine.Lands.Count),
            index => engine.Lands.PositionOf(index) != before[index]);
    }

    private static byte[] EventData(
        params (int Id, int Flags, sbyte Left, sbyte Top,
                byte Width, byte Height, int Parameter)[] rows)
    {
        const int blockOffset = 8;
        var data = new byte[
            blockOffset + 2 + rows.Length * EventPlacements.RecordStride];
        BinaryPrimitives.WriteUInt16LittleEndian(data, 1);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(2), 1);
        BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(4), blockOffset);
        BinaryPrimitives.WriteUInt16LittleEndian(
            data.AsSpan(blockOffset), (ushort)rows.Length);

        for (int i = 0; i < rows.Length; i++)
        {
            var row = rows[i];
            Span<byte> record = data.AsSpan(
                blockOffset + 2 + i * EventPlacements.RecordStride,
                EventPlacements.RecordStride);
            record[0] = 10;
            record[1] = 20;
            BinaryPrimitives.WriteUInt16LittleEndian(record[2..], (ushort)row.Id);
            BinaryPrimitives.WriteUInt16LittleEndian(record[4..], (ushort)row.Flags);
            record[6] = unchecked((byte)row.Left);
            record[7] = unchecked((byte)row.Top);
            record[8] = row.Width;
            record[9] = row.Height;
            BinaryPrimitives.WriteUInt16LittleEndian(
                record[10..], (ushort)row.Parameter);
        }
        return data;
    }
}
