using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

/// <summary>
/// Real-data checks that an act has a beginning and an end. These mount actual
/// stages, so they skip cleanly when the game is not installed beside the repo.
/// </summary>
public class ActFlowTests
{
    private const string Root =
        @"C:\Users\DavidErikGarciaArena\Downloads\Sonic 4 - Episode 2 (Beta 8)\Sonic 4 - Episode 2 (Beta 8)";

    private static GameEngine? Mount(string act)
    {
        if (!Directory.Exists(Root)) return null;
        var engine = new GameEngine(new FileSystemContent(Root)) { ActArchive = act };
        engine.Step();
        return engine;
    }

    [Fact]
    public void ThePlayerSpawnsAtTheActsOwnStartMarker()
    {
        var engine = Mount("G_ZONE1/MAP/ZONE11_MAP.AMB");
        if (engine is null) return;

        // The marker is at pixel 3,904; the engine converts through the same
        // scale everything else uses.
        float expected = 3904f * PlayerPhysics.WorldPerPixel;
        Assert.NotNull(engine.Player);
        Assert.Equal(expected, engine.Player!.Position.X, precision: 2);
    }

    [Fact]
    public void TheGoalStandsNearTheEndOfTheAct()
    {
        var engine = Mount("G_ZONE1/MAP/ZONE11_MAP.AMB");
        if (engine is null) return;

        Assert.NotNull(engine.GoalPosition);
        Assert.NotNull(engine.Collision);
        float actWidth = engine.Collision!.Width * engine.Collision.CellSize;
        Assert.True(engine.GoalPosition!.Value.X > actWidth * 0.6f,
                    $"goal at {engine.GoalPosition.Value.X} of {actWidth}");
    }

    [Fact]
    public void CrossingTheGoalClearsTheAct()
    {
        var engine = Mount("G_ZONE1/MAP/ZONE11_MAP.AMB");
        if (engine is null) return;

        Assert.False(engine.ActClear);
        // Teleport the player onto the goal rather than simulating six minutes
        // of running: the check under test is the crossing, not the journey.
        var goal = engine.GoalPosition!.Value;
        engine.Player!.PlaceOnGround(goal.X + 1f, goal.Y + 5f);
        engine.Step();

        Assert.True(engine.ActClear);
        Assert.StartsWith("ACT CLEAR", engine.Status);
    }

    [Fact]
    public void TheStartAndGoalAreStructurallyIdentifiedIds()
    {
        Assert.Equal(443, GameEngine.StartMarkerId);
        Assert.Equal(520, GameEngine.GoalPanelId);
    }
}
