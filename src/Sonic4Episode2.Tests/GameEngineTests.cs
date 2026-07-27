using Sonic4Episode2.Core.Engine;
using Xunit;

namespace Sonic4Episode2.Tests;

/// <summary>
/// Boot sequence and scene teardown, run against the real game data when it is
/// present.
/// </summary>
/// <remarks>
/// These need a copy of the game, so they skip rather than fail when it is
/// absent — a checkout on a machine without the data should still go green.
/// </remarks>
public class GameEngineTests
{
    /// <summary>Walks up from the test binary looking for the game root.</summary>
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

    [Fact]
    public void BootAdvancesToTheStageSceneAndMountsIt()
    {
        string? root = FindGameRoot();
        if (root is null) return;   // no game data here

        var engine = new GameEngine(root);

        // The boot scene requests its own exit on entry, so one step lands in
        // the stage scene.
        Assert.Equal(1, engine.Events.CurrentId);
        engine.Step();

        Assert.Equal(2, engine.Events.CurrentId);
        Assert.NotNull(engine.Stage);
        Assert.True(engine.Stage!.TriangleCount > 0);
    }

    [Fact]
    public void TheStageSceneRegistersItsTasksInTheSceneGroup()
    {
        string? root = FindGameRoot();
        if (root is null) return;

        var engine = new GameEngine(root);
        engine.Step();

        var sceneTasks = engine.Scheduler.Tasks
            .Where(t => t.Group == GameEngine.SceneGroup)
            .Select(t => t.Name)
            .ToList();

        Assert.Contains("GM_MAP_MAIN", sceneTasks);
        Assert.Contains("GM_EVT_MGR", sceneTasks);
    }

    [Fact]
    public void TasksRunInPriorityOrderOnceBooted()
    {
        string? root = FindGameRoot();
        if (root is null) return;

        var engine = new GameEngine(root);
        engine.Step();
        engine.Step();

        var priorities = engine.Scheduler.Tasks.Select(t => t.Priority).ToList();
        var sorted = priorities.OrderBy(p => p).ToList();
        Assert.Equal(sorted, priorities);
    }

    [Fact]
    public void TheFrameCounterAdvances()
    {
        string? root = FindGameRoot();
        if (root is null) return;

        var engine = new GameEngine(root);
        for (int i = 0; i < 5; i++) engine.Step();
        Assert.Equal(5ul, engine.Frame);
    }

    [Theory]
    [InlineData("G_ZONE1/MAP/ZONE11_MAP.AMB", "ZONE1_M.AMB")]
    [InlineData("G_ZONE4/MAP/ZONE42B_MAP.AMB", "ZONE4B_M.AMB")]
    [InlineData("G_ZONE3/MAP/ZONE33A_MAP.AMB", "ZONE3A_M.AMB")]
    public void TilesetResolutionHandlesBothNamingForms(string act, string expected)
    {
        string? root = FindGameRoot();
        if (root is null) return;

        string? tileset = GameEngine.FindTileset(Path.Combine(root, act));
        Assert.NotNull(tileset);
        Assert.Equal(expected, Path.GetFileName(tileset));
    }
}
