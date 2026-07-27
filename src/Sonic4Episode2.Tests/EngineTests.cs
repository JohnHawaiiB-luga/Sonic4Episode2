using Sonic4Episode2.Core.Engine;
using Xunit;

namespace Sonic4Episode2.Tests;

/// <summary>
/// The scheduler's contract, written down as assertions.
/// </summary>
/// <remarks>
/// These behaviours are subtle and every one of them is depended on somewhere in
/// the game, so they are pinned here rather than left to be rediscovered.
/// </remarks>
public class TaskSchedulerTests
{
    [Fact]
    public void RunsInPriorityOrder()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        var order = new List<string>();

        scheduler.Create("late", _ => order.Add("late"), priority: 300);
        scheduler.Create("early", _ => order.Add("early"), priority: 100);
        scheduler.Create("middle", _ => order.Add("middle"), priority: 200);
        scheduler.Step();

        Assert.Equal(["early", "middle", "late"], order);
    }

    [Fact]
    public void EqualPrioritiesKeepCreationOrder()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        var order = new List<string>();

        scheduler.Create("first", _ => order.Add("first"), priority: 100);
        scheduler.Create("second", _ => order.Add("second"), priority: 100);
        scheduler.Create("third", _ => order.Add("third"), priority: 100);
        scheduler.Step();

        Assert.Equal(["first", "second", "third"], order);
    }

    [Fact]
    public void ATaskMayDeleteItselfMidFrame()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        int ran = 0;

        TaskControlBlock? self = null;
        self = scheduler.Create("suicide", _ =>
        {
            ran++;
            scheduler.Delete(self!);
        }, priority: 100);

        scheduler.Step();
        scheduler.Step();

        Assert.Equal(1, ran);
        Assert.Equal(0, scheduler.Count);
    }

    [Fact]
    public void DeletingAnotherTaskDoesNotDisturbTheCurrentFrame()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        var order = new List<string>();

        var victim = scheduler.Create("victim", _ => order.Add("victim"), priority: 200);
        scheduler.Create("killer", _ =>
        {
            order.Add("killer");
            scheduler.Delete(victim);
        }, priority: 100);

        // The victim is already marked when the walk reaches it, so it is
        // skipped this frame rather than running one last time.
        scheduler.Step();
        Assert.Equal(["killer"], order);
        Assert.Equal(1, scheduler.Count);
    }

    [Fact]
    public void DestructorRunsOnDeleteAndOnlyOnce()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        int destroyed = 0;

        var task = scheduler.Create("t", _ => { }, priority: 100,
            destructor: _ => destroyed++);

        scheduler.Delete(task);
        scheduler.Delete(task);
        scheduler.Step();

        Assert.Equal(1, destroyed);
    }

    [Fact]
    public void CreatingDuringAFrameDefersToTheNextOne()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        var order = new List<string>();

        scheduler.Create("spawner", _ =>
        {
            order.Add("spawner");
            scheduler.Create("spawned", _ => order.Add("spawned"), priority: 50);
        }, priority: 100);

        scheduler.Step();
        Assert.Equal(["spawner"], order);

        // The new task has a lower priority number, so once it is really in the
        // list it runs first.
        scheduler.Step();
        Assert.Equal(["spawner", "spawned", "spawner"], order);
    }

    [Fact]
    public void PauseSkipsTasksAtOrBelowTheLevel()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        var order = new List<string>();

        scheduler.Create("gameplay", _ => order.Add("gameplay"), priority: 100, pauseLevel: 0);
        scheduler.Create("menu", _ => order.Add("menu"), priority: 200, pauseLevel: 5);

        scheduler.Step();
        Assert.Equal(["gameplay", "menu"], order);

        // Pausing to level 0 freezes the gameplay task but not the menu above it.
        order.Clear();
        scheduler.StartPause(0);
        scheduler.Step();
        Assert.Equal(["menu"], order);

        order.Clear();
        scheduler.EndPause();
        scheduler.Step();
        Assert.Equal(["gameplay", "menu"], order);
    }

    [Fact]
    public void ATaskCanBeImmuneToPausing()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        int ran = 0;

        scheduler.Create("always", _ => ran++, priority: 100, ignoresPause: true);

        scheduler.StartPause(9999);
        scheduler.Step();

        Assert.Equal(1, ran);
    }

    [Fact]
    public void GroupDeleteRemovesOnlyThatGroup()
    {
        var scheduler = new Core.Engine.TaskScheduler();

        scheduler.Create("a", _ => { }, priority: 100, group: 1);
        scheduler.Create("b", _ => { }, priority: 100, group: 1);
        scheduler.Create("c", _ => { }, priority: 100, group: 2);

        Assert.Equal(2, scheduler.DeleteGroup(1));
        scheduler.Step();

        Assert.Equal(1, scheduler.Count);
        Assert.Equal("c", scheduler.Tasks[0].Name);
    }
}

/// <summary>The scene state machine's contract.</summary>
public class EventSystemTests
{
    private static SceneDefinition Scene(string name, params int[] next)
    {
        var table = new int[EventSystem.BranchCount];
        for (int i = 0; i < next.Length && i < table.Length; i++) table[i] = next[i];
        return new SceneDefinition(name, table);
    }

    [Fact]
    public void EntersTheStartSceneImmediately()
    {
        var entered = new List<string>();
        var scenes = new List<SceneDefinition>
        {
            Scene("idle"),
            new("boot", new int[EventSystem.BranchCount], _ => entered.Add("boot")),
        };

        var system = new EventSystem(scenes, startId: 1);
        Assert.Equal(1, system.CurrentId);
        Assert.Equal(["boot"], entered);
    }

    [Fact]
    public void TransitionIsDeferredUntilTheNextStep()
    {
        var scenes = new List<SceneDefinition> { Scene("idle"), Scene("a", 2), Scene("b") };
        var system = new EventSystem(scenes, startId: 1);

        system.RequestChange();
        Assert.Equal(1, system.CurrentId);   // not yet

        Assert.True(system.Step());
        Assert.Equal(2, system.CurrentId);
        Assert.Equal(1, system.PreviousId);
    }

    [Fact]
    public void ExitAndEnterRunInOrder()
    {
        var log = new List<string>();
        var scenes = new List<SceneDefinition>
        {
            Scene("idle"),
            new("a", Next(2), null, () => log.Add("exit a"), null, () => log.Add("exit sys a")),
            new("b", new int[EventSystem.BranchCount],
                _ => log.Add("enter b"), null, () => log.Add("enter sys b")),
        };

        var system = new EventSystem(scenes, startId: 1);
        system.RequestChange();
        system.Step();

        Assert.Equal(["exit a", "exit sys a", "enter sys b", "enter b"], log);
    }

    [Fact]
    public void BranchSelectionPicksTheNamedSlot()
    {
        var scenes = new List<SceneDefinition>
        {
            Scene("idle"), Scene("menu", 2, 3), Scene("play"), Scene("options"),
        };
        var system = new EventSystem(scenes, startId: 1);

        system.DecideCase(1);
        system.RequestChange();
        system.Step();

        Assert.Equal(3, system.CurrentId);
    }

    [Fact]
    public void AnUnsetBranchFallsBackToSlotZero()
    {
        var scenes = new List<SceneDefinition>
        {
            Scene("idle"), Scene("menu", 2, 3), Scene("play"), Scene("options"),
        };
        var system = new EventSystem(scenes, startId: 1);

        system.DecideCase(5);   // nothing in slot 5
        system.RequestChange();
        system.Step();

        Assert.Equal(2, system.CurrentId);
    }

    [Fact]
    public void ArgumentIsCarriedToTheNextScene()
    {
        byte[]? received = null;
        var scenes = new List<SceneDefinition>
        {
            Scene("idle"),
            Scene("a", 2),
            new("b", new int[EventSystem.BranchCount], arg => received = arg),
        };

        var system = new EventSystem(scenes, startId: 1);
        system.RequestChange([7, 8, 9]);
        system.Step();

        Assert.NotNull(received);
        Assert.Equal(7, received![0]);
        Assert.Equal(8, received[1]);
        Assert.Equal(9, received[2]);
    }

    [Fact]
    public void SteppingWithNoRequestDoesNothing()
    {
        var scenes = new List<SceneDefinition> { Scene("idle"), Scene("a", 2), Scene("b") };
        var system = new EventSystem(scenes, startId: 1);

        Assert.False(system.Step());
        Assert.Equal(1, system.CurrentId);
    }

    private static int[] Next(params int[] values)
    {
        var table = new int[EventSystem.BranchCount];
        for (int i = 0; i < values.Length && i < table.Length; i++) table[i] = values[i];
        return table;
    }
}
