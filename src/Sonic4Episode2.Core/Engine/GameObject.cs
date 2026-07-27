// System.Numerics.Vector3 rather than a hand-rolled one: it is the standard
// type, it is SIMD-accelerated, and defining another Vector3 collides with the
// one every graphics framework already ships.
using System.Numerics;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// The base entity: everything in a stage that is not the map itself is one of
/// these — rings, springs, badniks, gimmicks, the player.
/// </summary>
/// <remarks>
/// Behaviour is supplied through a fixed set of procedure slots rather than by
/// subclassing, and the engine runs them in a **fixed order every frame**. That
/// order is the contract: an object's collision runs after its movement, its
/// draw registration after its collision, and so on. Objects rely on it.
/// <para>
/// The offset dance in <see cref="Update"/> is the subtlest part. Temporary
/// displacement — riding a platform, being pushed — is applied to
/// <see cref="TempOffset"/> rather than to the position directly. Each frame the
/// engine subtracts last frame's offset before running logic and adds this
/// frame's after, so a displacement that persists does not accumulate and one
/// that stops leaves no residue. Writing straight to the position instead looks
/// correct until something rides a moving platform.
/// </para>
/// </remarks>
public class GameObject
{
    public string Name { get; init; } = "object";

    public Vector3 Position;
    public Vector3 Offset;

    /// <summary>Displacement applied this frame; see the note on the class.</summary>
    public Vector3 TempOffset;

    private Vector3 _previousTempOffset;

    public GameObject? Parent { get; set; }

    /// <summary>Skipped while &gt; 0, decremented by the engine.</summary>
    public int InvincibleTimer;
    public int HitStopTimer;

    /// <summary>Pause level this object obeys; see <see cref="TaskScheduler"/>.</summary>
    public int PauseLevel { get; set; }

    public bool Destroyed { get; private set; }
    public bool DestroyRequested { get; set; }

    /// <summary>Set once the object's model or animation has finished loading.</summary>
    public bool AssetsReady { get; set; } = true;

    /// <summary>Runs logic even while paused — pause menus and HUD need it.</summary>
    public bool RunsWhilePaused { get; set; }

    // Procedure slots, run in the order declared here.

    /// <summary>Returns true to destroy the object when it leaves the view.</summary>
    public Func<GameObject, bool>? ViewCheck { get; set; }

    /// <summary>Early per-frame hook, before timers and main logic.</summary>
    public Action<GameObject>? OnEnter { get; set; }

    /// <summary>Main behaviour.</summary>
    public Action<GameObject>? OnUpdate { get; set; }

    /// <summary>Movement, applied after behaviour has decided what to do.</summary>
    public Action<GameObject>? OnMove { get; set; }

    /// <summary>Collision, after movement so it can correct the result.</summary>
    public Action<GameObject>? OnCollide { get; set; }

    /// <summary>Draw registration, after the position is final.</summary>
    public Action<GameObject>? OnRegisterDraw { get; set; }

    /// <summary>Last hook of the frame.</summary>
    public Action<GameObject>? OnLast { get; set; }

    /// <summary>Called once when the object is destroyed.</summary>
    public Action<GameObject>? OnDestroy { get; set; }

    public void Destroy()
    {
        if (Destroyed) return;
        Destroyed = true;
        OnDestroy?.Invoke(this);
    }

    /// <summary>
    /// Runs one frame of this object, in the engine's fixed order.
    /// </summary>
    /// <param name="paused">
    /// True when the object's pause level is currently frozen.
    /// </param>
    public void Update(bool paused = false)
    {
        if (Destroyed) return;

        if (DestroyRequested)
        {
            Destroy();
            return;
        }

        if (ViewCheck is not null && ViewCheck(this))
        {
            Destroy();
            return;
        }

        // A child follows its parent, and a destroyed parent takes it along.
        if (Parent is not null)
        {
            if (Parent.Destroyed)
            {
                Destroy();
                return;
            }
            Position = Parent.Position + Offset;
        }

        // Nothing runs until the object's assets exist. This gate is why objects
        // can be spawned the moment a stage streams in rather than waiting.
        if (!AssetsReady) return;

        Position -= _previousTempOffset;

        if (!paused || RunsWhilePaused)
            OnEnter?.Invoke(this);

        if (!paused)
        {
            if (HitStopTimer > 0) HitStopTimer--;

            // Hit-stop freezes behaviour but not the frame: timers below it keep
            // ticking, which is what makes a hit feel like impact rather than lag.
            if (HitStopTimer == 0)
            {
                if (InvincibleTimer > 0) InvincibleTimer--;
                OnUpdate?.Invoke(this);
            }

            OnMove?.Invoke(this);
            OnCollide?.Invoke(this);
        }

        Position += TempOffset;
        _previousTempOffset = TempOffset;

        if (!paused || RunsWhilePaused)
        {
            OnRegisterDraw?.Invoke(this);
            OnLast?.Invoke(this);
        }
    }

    public override string ToString() =>
        $"<{Name} at {Position}{(Destroyed ? " destroyed" : "")}>";
}

/// <summary>
/// Owns every object in a stage and steps them in creation order.
/// </summary>
/// <remarks>
/// Deletion is deferred to the end of the frame for the same reason as in the
/// task scheduler: an object may destroy itself or another from inside its own
/// update.
/// </remarks>
public sealed class ObjectManager
{
    private readonly List<GameObject> _objects = [];
    private readonly List<GameObject> _pending = [];
    private bool _running;

    public IReadOnlyList<GameObject> Objects => _objects;
    public int Count => _objects.Count;

    /// <summary>Runs before every object each frame.</summary>
    public Action<GameObject>? PreUpdate { get; set; }

    /// <summary>Runs after every object each frame.</summary>
    public Action<GameObject>? PostUpdate { get; set; }

    public T Add<T>(T instance) where T : GameObject
    {
        if (_running) _pending.Add(instance);
        else _objects.Add(instance);
        return instance;
    }

    public void Step(int pauseLevel = -1)
    {
        _running = true;
        try
        {
            foreach (var instance in _objects)
            {
                if (instance.Destroyed) continue;
                PreUpdate?.Invoke(instance);
                instance.Update(instance.PauseLevel <= pauseLevel);
                PostUpdate?.Invoke(instance);
            }
        }
        finally
        {
            _running = false;
        }

        _objects.RemoveAll(o => o.Destroyed);
        _objects.AddRange(_pending);
        _pending.Clear();
    }
}
