namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// The scene state machine: a fixed table of scenes, each naming up to eight
/// possible successors.
/// </summary>
/// <remarks>
/// This is the whole scene graph of the game — logos, title, menus, gameplay,
/// special stage, credits — expressed as a table rather than as code. Episode I
/// builds a 15-entry table covering exactly that set.
/// <para>
/// Transitions are <b>deferred by one step</b>: requesting a change sets a flag,
/// and the switch happens on the next <see cref="Step"/>. That is what lets a
/// scene request its own exit from inside its own update without unwinding
/// through code that is still running.
/// </para>
/// <para>
/// A scene whose successor table has nothing in slot 1 is treated as linear and
/// immediately arms slot 0 as its next destination; a scene with a real branch
/// waits to be told which way to go.
/// </para>
/// </remarks>
public sealed class EventSystem
{
    /// <summary>How many successors a scene may name.</summary>
    public const int BranchCount = 8;

    private readonly SceneDefinition[] _scenes;
    private bool _changeRequested;
    private bool _started;
    private int _requestedId = -1;
    private byte[] _argument = new byte[8];

    public EventSystem(IReadOnlyList<SceneDefinition> scenes, int startId)
    {
        _scenes = [.. scenes];
        if (startId < 0 || startId >= _scenes.Length)
            throw new ArgumentOutOfRangeException(nameof(startId));

        CurrentId = startId;
        ArmDefaultIfLinear();
    }

    /// <summary>Enters the start scene. Call once, after construction.</summary>
    /// <remarks>
    /// Deliberately not done in the constructor. A scene's enter callback
    /// routinely needs the event system itself — the boot scene requests its own
    /// transition — and during construction the field holding it is still null.
    /// Entering here instead means the whole object graph exists first.
    /// </remarks>
    public void Start()
    {
        if (_started) throw new InvalidOperationException("already started");
        _started = true;
        EnterCurrent();
    }

    public int CurrentId { get; private set; }
    public int PreviousId { get; private set; } = -1;

    public SceneDefinition Current => _scenes[CurrentId];

    /// <summary>The eight-byte payload a scene may hand its successor.</summary>
    public ReadOnlySpan<byte> Argument => _argument;

    /// <summary>Chooses a branch by slot, falling back to slot 0 when unset.</summary>
    public void DecideCase(int slot)
    {
        if (slot < 0 || slot >= BranchCount) slot = 0;
        if (Current.Next[slot] == 0) slot = 0;
        Decide(Current.Next[slot]);
    }

    /// <summary>Chooses a branch by target id, provided this scene offers it.</summary>
    public void DecideById(int targetId)
    {
        int slot = 0;
        while (slot < BranchCount && Current.Next[slot] != 0 && Current.Next[slot] != targetId)
            slot++;
        if (slot >= BranchCount || Current.Next[slot] == 0)
            targetId = Current.Next[0];
        Decide(targetId);
    }

    private void Decide(int id)
    {
        // Zero is "unset" in the successor table rather than a valid scene, so a
        // scene cannot be entered by falling off the end of the branch list.
        if (id <= 0 || id >= _scenes.Length) return;
        _requestedId = id;
    }

    /// <summary>Arms the transition; it takes effect on the next step.</summary>
    public void RequestChange(ReadOnlySpan<byte> argument = default)
    {
        if (_requestedId < 0) _requestedId = Current.Next[0];
        _changeRequested = true;

        _argument = new byte[8];
        if (!argument.IsEmpty)
            argument[..Math.Min(argument.Length, 8)].CopyTo(_argument);
    }

    /// <summary>Performs a pending transition. Call once per frame.</summary>
    /// <returns>True when a scene change happened this step.</returns>
    public bool Step()
    {
        if (!_changeRequested) return false;
        if (_requestedId < 0 || _requestedId >= _scenes.Length)
        {
            _changeRequested = false;
            return false;
        }

        Current.Exit?.Invoke();
        Current.ExitSystem?.Invoke();

        PreviousId = CurrentId;
        CurrentId = _requestedId;
        _requestedId = -1;
        _changeRequested = false;

        ArmDefaultIfLinear();
        EnterCurrent();
        return true;
    }

    private void ArmDefaultIfLinear()
    {
        // A scene with nothing in slot 1 has no real choice to make, so its only
        // successor is armed straight away. A branching scene waits for a
        // DecideCase call instead.
        if (Current.Next[1] <= 0)
            Decide(Current.Next[0]);
    }

    private void EnterCurrent()
    {
        Current.EnterSystem?.Invoke();
        Current.Enter?.Invoke(_argument);
    }
}

/// <summary>One row of the scene table.</summary>
/// <param name="Name">For diagnostics only; the engine keys on the index.</param>
/// <param name="Next">
/// Successor scene ids. Slot 0 is the default; zero means unused.
/// </param>
public sealed record SceneDefinition(
    string Name,
    int[] Next,
    Action<byte[]>? Enter = null,
    Action? Exit = null,
    Action? EnterSystem = null,
    Action? ExitSystem = null)
{
    public static SceneDefinition Linear(string name, int next,
        Action<byte[]>? enter = null, Action? exit = null)
    {
        var table = new int[EventSystem.BranchCount];
        table[0] = next;
        return new SceneDefinition(name, table, enter, exit);
    }

    public bool Branches => Next.Length > 1 && Next[1] > 0;
}
