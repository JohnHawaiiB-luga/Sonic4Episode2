namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// The engine's cooperative task scheduler: a priority-ordered list of control
/// blocks, stepped once per frame.
/// </summary>
/// <remarks>
/// Every subsystem in the game hangs off this — the event system, the map, the
/// object manager, each individual object. AliceNN splits it across two layers
/// (<c>amTask</c> holding the list, <c>mtTask</c> adding pause levels and typed
/// work); that split is an artefact of the original C, so this merges them.
/// <para>
/// Three behaviours matter and are easy to get wrong:
/// </para>
/// <list type="number">
/// <item><b>Priority order.</b> A new task is inserted before the first task
/// whose priority is greater than its own, so equal priorities run in creation
/// order.</item>
/// <item><b>Deferred deletion.</b> Deleting marks the task and runs its
/// destructor immediately, but unlinking happens in a second pass after every
/// procedure has run. A task may therefore delete itself, or any other task,
/// mid-frame without corrupting the walk.</item>
/// <item><b>The pause gate is inverted from the obvious reading.</b> A task is
/// <i>skipped</i> when its own pause level is less than or equal to the system
/// pause level. The system level is -1 when nothing is paused, so a task at
/// level 0 runs normally; pausing to level 0 then freezes it.</item>
/// </list>
/// </remarks>
public sealed class TaskScheduler
{
    private readonly List<TaskControlBlock> _tasks = [];
    private readonly List<TaskControlBlock> _pending = [];
    private bool _running;

    /// <summary>-1 when nothing is paused.</summary>
    public int PauseLevel { get; private set; } = -1;

    private int _requestedPauseLevel = -1;

    public int Count => _tasks.Count;

    public IReadOnlyList<TaskControlBlock> Tasks => _tasks;

    /// <summary>Creates a task and inserts it at its priority position.</summary>
    public TaskControlBlock Create(
        string name,
        Action<TaskControlBlock> procedure,
        int priority,
        Action<TaskControlBlock>? destructor = null,
        int group = 0,
        int pauseLevel = 0,
        bool ignoresPause = false,
        object? work = null)
    {
        var task = new TaskControlBlock(name, procedure, destructor, priority, group, work)
        {
            // Immunity is expressed as a pause level nothing can reach, matching
            // how the original flags a task as never-pausing.
            PauseLevel = ignoresPause ? int.MaxValue : pauseLevel,
        };

        // Creating a task from inside a running procedure must not disturb the
        // walk, so it queues until the frame ends.
        if (_running) _pending.Add(task);
        else Insert(task);
        return task;
    }

    private void Insert(TaskControlBlock task)
    {
        int at = 0;
        while (at < _tasks.Count && _tasks[at].Priority <= task.Priority) at++;
        _tasks.Insert(at, task);
    }

    /// <summary>Marks a task for removal and runs its destructor now.</summary>
    public void Delete(TaskControlBlock task)
    {
        if (task.Deleted) return;
        task.Deleted = true;
        task.Destructor?.Invoke(task);
    }

    /// <summary>Deletes every task in a group — how a scene tears itself down.</summary>
    public int DeleteGroup(int group)
    {
        int deleted = 0;
        foreach (var task in _tasks)
        {
            if (task.Group != group || task.Deleted) continue;
            Delete(task);
            deleted++;
        }
        return deleted;
    }

    /// <summary>Freezes every task whose pause level is at or below this one.</summary>
    public void StartPause(int level) => _requestedPauseLevel = level;

    public void EndPause() => _requestedPauseLevel = -1;

    /// <summary>Runs one frame: every live task in priority order, then cleanup.</summary>
    public void Step()
    {
        // The pause level takes effect at a frame boundary, so a task cannot
        // pause itself half way through a frame and see inconsistent state.
        PauseLevel = _requestedPauseLevel;

        _running = true;
        try
        {
            for (int i = 0; i < _tasks.Count; i++)
            {
                var task = _tasks[i];
                if (task.Deleted) continue;
                if (task.PauseLevel <= PauseLevel) continue;
                task.Procedure(task);
            }
        }
        finally
        {
            _running = false;
        }

        _tasks.RemoveAll(t => t.Deleted);
        foreach (var task in _pending) Insert(task);
        _pending.Clear();
    }
}

/// <summary>One scheduled task.</summary>
public sealed class TaskControlBlock(
    string name,
    Action<TaskControlBlock> procedure,
    Action<TaskControlBlock>? destructor,
    int priority,
    int group,
    object? work)
{
    public string Name { get; } = name;
    public Action<TaskControlBlock> Procedure { get; set; } = procedure;
    public Action<TaskControlBlock>? Destructor { get; set; } = destructor;
    public int Priority { get; } = priority;
    public int Group { get; } = group;

    /// <summary>Per-task state, cast by the owning subsystem.</summary>
    public object? Work { get; set; } = work;

    public int PauseLevel { get; set; }
    public bool Deleted { get; internal set; }

    /// <summary>Frames this task has run, which several object states count on.</summary>
    public ulong Ticks { get; set; }

    public T WorkAs<T>() where T : class =>
        Work as T ?? throw new InvalidOperationException(
            $"task '{Name}' work is {Work?.GetType().Name ?? "null"}, not {typeof(T).Name}");

    public override string ToString() => $"<Task {Name} prio={Priority} group={Group}>";
}
