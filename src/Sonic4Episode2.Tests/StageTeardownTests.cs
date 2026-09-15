using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class StageTeardownTests
{
    [Fact]
    public void ClearDestroysObjectsOnceAndDropsObjectsCreatedByDestruction()
    {
        var manager = new ObjectManager();
        int primaryDestroyed = 0;
        int secondaryDestroyed = 0;
        int spawnedDestroyed = 0;
        GameObject? spawned = null;

        var primary = new GameObject
        {
            OnDestroy = _ =>
            {
                primaryDestroyed++;
                manager.Clear();
                spawned = manager.Add(new GameObject
                {
                    OnDestroy = _ => spawnedDestroyed++,
                });
            },
        };
        var secondary = new GameObject { OnDestroy = _ => secondaryDestroyed++ };

        manager.Add(primary);
        manager.Add(secondary);

        manager.Clear();
        manager.Clear();

        Assert.True(primary.Destroyed);
        Assert.True(secondary.Destroyed);
        Assert.NotNull(spawned);
        Assert.True(spawned!.Destroyed);
        Assert.Equal(1, primaryDestroyed);
        Assert.Equal(1, secondaryDestroyed);
        Assert.Equal(1, spawnedDestroyed);
        Assert.Empty(manager.Objects);
    }

    [Fact]
    public void ClearRejectsCallsFromStepBeforeDestroyingObjects()
    {
        var manager = new ObjectManager();
        int destroyed = 0;
        var instance = manager.Add(new GameObject
        {
            OnUpdate = _ => Assert.Throws<InvalidOperationException>(manager.Clear),
            OnDestroy = _ => destroyed++,
        });

        manager.Step();

        Assert.False(instance.Destroyed);
        Assert.Equal(0, destroyed);
        Assert.Single(manager.Objects);
    }

    [Fact]
    public void DeleteGroupIncludesQueuedTasksAndRejectsSameGroupResurrection()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        int activeDestroyed = 0;
        int queuedDestroyed = 0;
        int replacementDestroyed = 0;
        int unrelatedDestroyed = 0;
        int deleted = 0;
        TaskControlBlock? queued = null;
        TaskControlBlock? replacement = null;

        var active = scheduler.Create("active", _ => { }, priority: 100,
            destructor: _ =>
            {
                activeDestroyed++;
                replacement = scheduler.Create("replacement", _ => { }, priority: 100,
                    destructor: _ => replacementDestroyed++, group: 10);
            }, group: 10);
        var unrelated = scheduler.Create("unrelated", _ => { }, priority: 200,
            destructor: _ => unrelatedDestroyed++, group: 20);
        scheduler.Create("driver", _ =>
        {
            queued = scheduler.Create("queued", _ => { }, priority: 50,
                destructor: _ => queuedDestroyed++, group: 10);
            deleted = scheduler.DeleteGroup(10);
        }, priority: 0, group: 20);

        scheduler.Step();

        Assert.Equal(2, deleted);
        Assert.True(active.Deleted);
        Assert.NotNull(queued);
        Assert.True(queued!.Deleted);
        Assert.NotNull(replacement);
        Assert.True(replacement!.Deleted);
        Assert.Equal(1, activeDestroyed);
        Assert.Equal(1, queuedDestroyed);
        Assert.Equal(1, replacementDestroyed);
        Assert.Equal(0, unrelatedDestroyed);
        Assert.Equal(2, scheduler.Count);
        Assert.All(scheduler.Tasks, task => Assert.Equal(20, task.Group));
        Assert.False(unrelated.Deleted);
    }

    [Fact]
    public void DeleteGroupHandlesReentrantDestructionWithoutChangingItsWalk()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        var destruction = new List<string>();
        int reentrantDeleted = -1;
        TaskControlBlock? replacement = null;

        scheduler.Create("first", _ => { }, priority: 100,
            destructor: _ =>
            {
                destruction.Add("first");
                reentrantDeleted = scheduler.DeleteGroup(42);
                replacement = scheduler.Create("replacement", _ => { }, priority: 150,
                    destructor: _ => destruction.Add("replacement"), group: 42);
            }, group: 42);
        scheduler.Create("second", _ => { }, priority: 200,
            destructor: _ => destruction.Add("second"), group: 42);
        var unrelated = scheduler.Create("unrelated", _ => { }, priority: 300, group: 43);

        Assert.Equal(2, scheduler.DeleteGroup(42));
        scheduler.Step();

        Assert.Equal(0, reentrantDeleted);
        Assert.NotNull(replacement);
        Assert.True(replacement!.Deleted);
        Assert.Equal(["first", "replacement", "second"], destruction);
        Assert.Single(scheduler.Tasks);
        Assert.Same(unrelated, scheduler.Tasks[0]);
    }

    [Fact]
    public void StepDoesNotInsertPendingTasksDeletedBeforeCleanup()
    {
        var scheduler = new Core.Engine.TaskScheduler();
        int destroyed = 0;
        TaskControlBlock? pending = null;

        var driver = scheduler.Create("driver", _ =>
        {
            pending = scheduler.Create("pending", _ => { }, priority: 100,
                destructor: _ => destroyed++, group: 2);
            scheduler.Delete(pending);
        }, priority: 0, group: 1);

        scheduler.Step();

        Assert.NotNull(pending);
        Assert.True(pending!.Deleted);
        Assert.Equal(1, destroyed);
        Assert.Single(scheduler.Tasks);
        Assert.Same(driver, scheduler.Tasks[0]);
    }
}
