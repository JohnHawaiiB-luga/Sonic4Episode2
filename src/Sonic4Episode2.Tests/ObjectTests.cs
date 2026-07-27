using System.Numerics;
using Sonic4Episode2.Core.Engine;
using Xunit;

namespace Sonic4Episode2.Tests;

/// <summary>
/// The object contract: the fixed procedure order, and the offset handling that
/// makes riding a moving platform work.
/// </summary>
public class GameObjectTests
{
    [Fact]
    public void ProceduresRunInTheFixedOrder()
    {
        var log = new List<string>();
        var instance = new GameObject
        {
            OnEnter = _ => log.Add("enter"),
            OnUpdate = _ => log.Add("update"),
            OnMove = _ => log.Add("move"),
            OnCollide = _ => log.Add("collide"),
            OnRegisterDraw = _ => log.Add("draw"),
            OnLast = _ => log.Add("last"),
        };

        instance.Update();

        // Collision after movement so it can correct the result; draw after
        // collision so it sees the final position.
        Assert.Equal(["enter", "update", "move", "collide", "draw", "last"], log);
    }

    [Fact]
    public void TempOffsetDoesNotAccumulateAcrossFrames()
    {
        var instance = new GameObject { Position = new Vector3(100, 0, 0) };

        // A platform pushes the object 5 units right, every frame.
        instance.TempOffset = new Vector3(5, 0, 0);
        instance.Update();
        Assert.Equal(105f, instance.Position.X);

        instance.Update();
        Assert.Equal(105f, instance.Position.X);

        instance.Update();
        Assert.Equal(105f, instance.Position.X);
    }

    [Fact]
    public void ClearingTempOffsetLeavesNoResidue()
    {
        var instance = new GameObject { Position = new Vector3(100, 0, 0) };

        instance.TempOffset = new Vector3(5, 0, 0);
        instance.Update();
        Assert.Equal(105f, instance.Position.X);

        // Stepping off the platform must return the object exactly, not
        // strand it 5 units along.
        instance.TempOffset = System.Numerics.Vector3.Zero;
        instance.Update();
        Assert.Equal(100f, instance.Position.X);
    }

    [Fact]
    public void MovementStillAppliesUnderneathATempOffset()
    {
        var instance = new GameObject { Position = new Vector3(0, 0, 0) };
        instance.OnMove = o => o.Position += new Vector3(10, 0, 0);
        instance.TempOffset = new Vector3(3, 0, 0);

        instance.Update();
        Assert.Equal(13f, instance.Position.X);

        instance.Update();
        Assert.Equal(23f, instance.Position.X);
    }

    [Fact]
    public void ViewCheckDestroysTheObjectAndStopsTheFrame()
    {
        bool updated = false;
        var instance = new GameObject
        {
            ViewCheck = _ => true,
            OnUpdate = _ => updated = true,
        };

        instance.Update();

        Assert.True(instance.Destroyed);
        Assert.False(updated);
    }

    [Fact]
    public void AssetGateBlocksEverythingUntilReady()
    {
        int updates = 0;
        var instance = new GameObject
        {
            AssetsReady = false,
            OnUpdate = _ => updates++,
        };

        instance.Update();
        Assert.Equal(0, updates);

        instance.AssetsReady = true;
        instance.Update();
        Assert.Equal(1, updates);
    }

    [Fact]
    public void PauseStopsLogicButDrawStillRunsForPauseAwareObjects()
    {
        var log = new List<string>();
        var hud = new GameObject
        {
            RunsWhilePaused = true,
            OnEnter = _ => log.Add("enter"),
            OnUpdate = _ => log.Add("update"),
            OnRegisterDraw = _ => log.Add("draw"),
        };

        hud.Update(paused: true);

        // A HUD must keep drawing while paused, but must not run its logic.
        Assert.Equal(["enter", "draw"], log);
    }

    [Fact]
    public void HitStopFreezesBehaviourAndReleasesOnTheFrameItExpires()
    {
        int updates = 0;
        var instance = new GameObject { HitStopTimer = 2, OnUpdate = _ => updates++ };

        instance.Update();
        Assert.Equal(0, updates);
        Assert.Equal(1, instance.HitStopTimer);

        // The timer is decremented before the gate is tested, so the frame that
        // takes it to zero is the frame behaviour resumes - not the one after.
        // Getting this backwards costs one frame of input response on every hit,
        // which is exactly the kind of thing that feels wrong and reads fine.
        instance.Update();
        Assert.Equal(1, updates);
        Assert.Equal(0, instance.HitStopTimer);

        instance.Update();
        Assert.Equal(2, updates);
    }

    [Fact]
    public void InvincibilityIsHeldWhileHitStopIsStillRunning()
    {
        var instance = new GameObject { InvincibleTimer = 10, HitStopTimer = 2 };

        instance.Update();
        Assert.Equal(10, instance.InvincibleTimer);   // held: hit-stop still at 1

        // Hit-stop reaches zero here, so invincibility resumes the same frame.
        instance.Update();
        Assert.Equal(9, instance.InvincibleTimer);
    }

    [Fact]
    public void AChildFollowsItsParentAndDiesWithIt()
    {
        var parent = new GameObject { Position = new Vector3(50, 20, 0) };
        var child = new GameObject { Parent = parent, Offset = new Vector3(0, 10, 0) };

        child.Update();
        Assert.Equal(50f, child.Position.X);
        Assert.Equal(30f, child.Position.Y);

        parent.Destroy();
        child.Update();
        Assert.True(child.Destroyed);
    }
}

public class ObjectManagerTests
{
    [Fact]
    public void ObjectsMayDestroyOthersMidFrame()
    {
        var manager = new ObjectManager();
        int victimUpdates = 0;

        // Objects step in creation order, so the killer has to be added first
        // for the victim to be dead by the time the walk reaches it.
        var victim = new GameObject { Name = "victim", OnUpdate = _ => victimUpdates++ };
        manager.Add(new GameObject { Name = "killer", OnUpdate = _ => victim.Destroy() });
        manager.Add(victim);

        manager.Step();

        Assert.Equal(0, victimUpdates);
        Assert.Equal(1, manager.Count);
    }

    [Fact]
    public void AnObjectAlreadySteppedThisFrameStillDiesAtTheEndOfIt()
    {
        var manager = new ObjectManager();
        int victimUpdates = 0;

        var victim = new GameObject { Name = "victim", OnUpdate = _ => victimUpdates++ };
        manager.Add(victim);
        manager.Add(new GameObject { Name = "killer", OnUpdate = _ => victim.Destroy() });

        manager.Step();

        // It ran once before being killed, which is correct - the kill came
        // afterwards in the same frame.
        Assert.Equal(1, victimUpdates);
        Assert.Equal(1, manager.Count);
    }

    [Fact]
    public void SpawningMidFrameDefersToTheNextOne()
    {
        var manager = new ObjectManager();
        int spawnedUpdates = 0;

        manager.Add(new GameObject
        {
            Name = "spawner",
            OnUpdate = _ => manager.Add(new GameObject
            {
                Name = "spawned",
                OnUpdate = _ => spawnedUpdates++,
            }),
        });

        manager.Step();
        Assert.Equal(0, spawnedUpdates);
        Assert.Equal(2, manager.Count);

        manager.Step();
        Assert.Equal(1, spawnedUpdates);
    }

    [Fact]
    public void HooksBracketEveryObject()
    {
        var log = new List<string>();
        var manager = new ObjectManager
        {
            PreUpdate = o => log.Add($"pre:{o.Name}"),
            PostUpdate = o => log.Add($"post:{o.Name}"),
        };

        manager.Add(new GameObject { Name = "a", OnUpdate = _ => log.Add("a") });
        manager.Add(new GameObject { Name = "b", OnUpdate = _ => log.Add("b") });
        manager.Step();

        Assert.Equal(["pre:a", "a", "post:a", "pre:b", "b", "post:b"], log);
    }
}
