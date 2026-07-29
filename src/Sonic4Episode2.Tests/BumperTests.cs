using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class BumperTests
{
    private static string? FindGameRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(
                    directory.FullName, "G_EP1ZONE2", "MAP")))
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

    private static Placement Bumper(
        int id,
        int x = 100,
        int y = 100,
        int flags = 0) =>
        new(x, y, id, flags, 0);

    private static Vector2 Contact(
        Bumpers bumpers,
        float nativeX,
        float nativeY)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        return bumpers.PositionOf(0) +
               new Vector2(nativeX * scale, -nativeY * scale);
    }

    [Fact]
    public void OnlyCatalogBumperPlacementsBecomeBumpers()
    {
        var bumpers = new Bumpers(
        [
            Bumper(150),
            Bumper(148),
            Bumper(166),
            Bumper(9999),
        ]);

        Assert.Equal(1, bumpers.Count);
        Assert.Equal(150, bumpers.ObjectIdAt(0));
    }

    [Theory]
    [InlineData(150, -48, 0, 48, 28, 32768)]
    [InlineData(151, -48, -28, 48, 0, 0)]
    [InlineData(152, 0, -48, 28, 48, 16384)]
    [InlineData(153, -28, -48, 0, 48, 49152)]
    [InlineData(154, 0, 0, 64, 64, 16384)]
    [InlineData(155, 0, -64, 64, 0, 0)]
    [InlineData(156, -64, 0, 0, 64, 32768)]
    [InlineData(157, -64, -64, 0, 0, 49152)]
    [InlineData(158, -24, -8, 24, 8, 0)]
    [InlineData(159, -8, -24, 8, 24, 16384)]
    public void VariantsUseRecoveredHitboxesAndAngles(
        int id,
        int left,
        int top,
        int right,
        int bottom,
        int angleA16)
    {
        var bumpers = new Bumpers([Bumper(id)]);

        Assert.Equal(
            new BumperHitbox(left, top, right, bottom),
            bumpers.HitboxAt(0));
        Assert.Equal(angleA16, bumpers.AngleA16At(0));
    }

    [Theory]
    [InlineData(150, 0, 10, 0, -6, 5)]
    [InlineData(151, 0, -10, 0, 6, 5)]
    [InlineData(152, 10, 0, 4, 0, 15)]
    [InlineData(153, -10, 0, -4, 0, 15)]
    [InlineData(154, 10, 10, 4, -5, 5)]
    [InlineData(155, 10, -10, 4, 5, 5)]
    [InlineData(156, -10, 10, -4, -5, 5)]
    [InlineData(157, -10, -10, -4, 5, 5)]
    [InlineData(158, 0, -10, 0, 6, 5)]
    [InlineData(159, -10, 0, -4, 0, 15)]
    public void VariantsLaunchInTheirRecoveredDirections(
        int id,
        float nativeX,
        float nativeY,
        float expectedX,
        float expectedY,
        int controlLockFrames)
    {
        var bumpers = new Bumpers([Bumper(id)]);
        float scale = PlayerPhysics.WorldPerPixel;

        BumperImpact? impact = bumpers.Check(
            Contact(bumpers, nativeX, nativeY),
            Vector2.Zero);

        Assert.NotNull(impact);
        Assert.Equal(expectedX * scale, impact.Value.Velocity.X, 5);
        Assert.Equal(expectedY * scale, impact.Value.Velocity.Y, 5);
        Assert.Equal(controlLockFrames, impact.Value.ControlLockFrames);
    }

    [Fact]
    public void OffCenterHitAddsThreePixelsThenClampsAtFour()
    {
        var bumpers = new Bumpers([Bumper(150)]);
        float scale = PlayerPhysics.WorldPerPixel;

        BumperImpact? impact = bumpers.Check(
            Contact(bumpers, 20, 10),
            new Vector2(2f * scale, 0f));

        Assert.NotNull(impact);
        Assert.Equal(4f * scale, impact.Value.Velocity.X, 5);
        Assert.Equal(-6f * scale, impact.Value.Velocity.Y, 5);
    }

    [Fact]
    public void LaunchUsesRecoveredThreePixelVerticalOriginOffset()
    {
        var bumpers = new Bumpers([Bumper(159)]);
        float scale = PlayerPhysics.WorldPerPixel;

        BumperImpact? impact = bumpers.Check(
            Contact(bumpers, -10, -6),
            Vector2.Zero);

        Assert.NotNull(impact);
        Assert.Equal(-4f * scale, impact.Value.Velocity.X, 5);
        Assert.Equal(3f * scale, impact.Value.Velocity.Y, 5);
    }

    [Fact]
    public void SlopedVariantRejectsItsBackCorner()
    {
        var bumpers = new Bumpers([Bumper(150)]);

        Assert.Null(bumpers.Check(
            Contact(bumpers, 45, 26),
            Vector2.Zero));
    }

    [Fact]
    public void BumperMustBeLeftBeforeItCanFireAgain()
    {
        var bumpers = new Bumpers([Bumper(154)]);
        Vector2 contact = Contact(bumpers, 10, 10);

        Assert.NotNull(bumpers.Check(contact, Vector2.Zero));
        Assert.Null(bumpers.Check(contact, Vector2.Zero));
        Assert.Null(bumpers.Check(contact + new Vector2(1000f), Vector2.Zero));
        Assert.NotNull(bumpers.Check(contact, Vector2.Zero));
    }

    [Fact]
    public void PlayerControlStaysLockedForRecoveredFiveFrames()
    {
        var bumpers = new Bumpers([Bumper(154)]);
        var player = EmptyPlayer();
        Vector2 center = Contact(bumpers, 10, 10);
        player.Position = new Vector3(
            center.X,
            center.Y - Player.Height / 2f,
            0f);

        Assert.True(bumpers.Check(player));
        float launchedX = player.Velocity.X;
        player.InputX = -1f;

        for (int i = 0; i < 5; i++)
        {
            player.Update();
            Assert.Equal(launchedX, player.Velocity.X, 5);
        }

        player.Update();
        Assert.True(player.Velocity.X < launchedX);
    }

    [Fact]
    public void EngineMountsEveryRealEpisodeMetalBumper()
    {
        string? root = FindGameRoot();
        if (root is null) return;

        var engine = new GameEngine(new FileSystemContent(root))
        {
            ActArchive = "G_EP1ZONE2/MAP/ZONE21_MAP.AMB",
        };
        engine.Step();

        Assert.NotNull(engine.Bumpers);
        Assert.Equal(118, engine.Bumpers!.Count);
        Assert.Equal(
            engine.Placements.Count(
                placement => ObjectCatalog.Is(placement.ObjectId, "Bumper")),
            engine.Bumpers.Count);
    }
}
