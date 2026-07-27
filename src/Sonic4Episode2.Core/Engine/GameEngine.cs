using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// Ties the scheduler, the scene machine and the object manager together and
/// drives them from one frame step.
/// </summary>
/// <remarks>
/// The layering matters. The scene machine decides *what* is running, the
/// scheduler decides *in what order* within a frame, and the object manager
/// holds the entities the current scene created. A scene tears itself down by
/// deleting its task group, which is why every task a scene creates carries that
/// scene's group id.
/// <para>
/// Boot order within a frame is: advance the scene machine first so a pending
/// transition happens before anything runs, then step the scheduler, then step
/// the objects. A scene that requests its own exit therefore finishes its frame
/// normally and disappears at the start of the next one.
/// </para>
/// </remarks>
public sealed class GameEngine
{
    /// <summary>Task group owned by the running scene; freed on every transition.</summary>
    public const int SceneGroup = 1;

    /// <summary>Task priorities, low to high, matching the engine's own ordering.</summary>
    public const int PriorityMap = 0x1000;
    public const int PriorityObject = 0x2000;
    public const int PriorityCamera = 0x3000;

    private readonly string _gameRoot;

    public GameEngine(string gameRoot)
    {
        _gameRoot = gameRoot;
        Scheduler = new TaskScheduler();
        Objects = new ObjectManager();

        var scenes = new List<SceneDefinition>
        {
            // Index 0 is the idle slot: the successor table uses 0 to mean
            // "unset", so no real scene may live there.
            new("idle", new int[EventSystem.BranchCount]),
            SceneDefinition.Linear("boot", next: 2, enter: _ => EnterBoot()),
            new("stage", new int[EventSystem.BranchCount], _ => EnterStage(), ExitStage),
        };

        Events = new EventSystem(scenes, startId: 1);
        // Only now that every field is assigned is it safe to run scene
        // callbacks, which reach back into this object.
        Events.Start();
    }

    public TaskScheduler Scheduler { get; }
    public EventSystem Events { get; }
    public ObjectManager Objects { get; }

    /// <summary>The stage currently mounted, if any.</summary>
    public StageBatch? Stage { get; private set; }

    /// <summary>Solidity for the mounted stage, from its attribute layer.</summary>
    public CollisionMap? Collision { get; private set; }

    /// <summary>The player, once a stage scene has created one.</summary>
    public Player? Player { get; private set; }

    /// <summary>Object placements read from the stage's `.EV` files.</summary>
    public IReadOnlyList<Placement> Placements { get; private set; } = [];

    public string? StageName { get; private set; }
    public ulong Frame { get; private set; }

    /// <summary>Act archive the stage scene will mount, relative to the root.</summary>
    public string ActArchive { get; set; } = "G_ZONE1/MAP/ZONE11_MAP.AMB";

    /// <summary>Diagnostics from the last mount.</summary>
    public string Status { get; private set; } = "";

    private void EnterBoot()
    {
        // Nothing to do yet beyond existing: the boot scene is where global
        // systems would be brought up. It advances immediately.
        Status = "boot";
        Events.RequestChange();
    }

    private void EnterStage()
    {
        string actPath = Path.Combine(_gameRoot, ActArchive);
        var archive = AmbArchive.Load(actPath);

        string? tilesetPath = FindTileset(actPath);
        if (tilesetPath is null)
            throw new InvalidOperationException($"no tileset archive beside {actPath}");

        var assembler = new StageAssembler(AmbArchive.Load(tilesetPath));
        var batch = new StageBatch();
        var placements = new List<Placement>();

        // Ground shapes live in the zone's ATTR archive, beside the act.
        CollisionShapes? shapes = LoadShapes(actPath);
        StageGrid? attributeGrid = null;

        foreach (var entry in archive.Entries)
        {
            string label = entry.Name.Replace('\\', '/');
            label = label[(label.LastIndexOf('/') + 1)..];
            // Collision comes from the attribute layer, which is a superset of
            // the visual one: it also carries invisible walls and ceilings.
            if (label.EndsWith("_ATTR_B.MP", StringComparison.OrdinalIgnoreCase))
            {
                attributeGrid = StageGrid.Parse(label, archive.Read(entry).Span);
                continue;
            }
            if (label.EndsWith(".EV", StringComparison.OrdinalIgnoreCase))
            {
                // Three variants ship per act - ZONE11.EV, ZONE11A.EV,
                // ZONE11C.EV - and what selects between them is not known. Take
                // the base one, which carries the main object set: it is the
                // variant whose name ends in a digit rather than a letter.
                string stem = label[..^3];
                if (stem.Length > 0 && char.IsDigit(stem[^1]))
                    placements.AddRange(
                        EventPlacements.Parse(archive.Read(entry).Span).Items);
                continue;
            }
            if (!label.EndsWith("_B.MP", StringComparison.OrdinalIgnoreCase)) continue;

            var grid = StageGrid.Parse(label, archive.Read(entry).Span);
            assembler.AddLayer(grid, "_B", batch);
        }

        if (attributeGrid is not null)
            Collision = CollisionMap.FromGrid(attributeGrid, shapes);

        Stage = batch;
        Placements = placements;
        StageName = Path.GetFileNameWithoutExtension(actPath);
        Status = $"{assembler.TilesPlaced} tiles, {batch.VertexCount:N0} vertices, " +
                 $"{batch.TriangleCount:N0} triangles, {placements.Count} placements" +
                 (Collision?.HasShapes == true ? ", height fields" : ", blocky collision");

        // The map is a task like anything else, so it obeys pause levels and is
        // torn down with the scene rather than by special-case code.
        Scheduler.Create("GM_MAP_MAIN", _ => { }, PriorityMap, group: SceneGroup);
        Scheduler.Create("GM_EVT_MGR", _ => Objects.Step(Scheduler.PauseLevel),
                         PriorityObject, group: SceneGroup);

        if (Collision is not null)
        {
            Player = Objects.Add(new Player(Collision));
            // Drop onto whatever is below rather than guessing a spawn point.
            // The real one comes from the .EV placement data, once object ids
            // have names attached to them.
            Player.PlaceOnGround(
                Collision.Width * Collision.CellSize * 0.06f,
                -Collision.Height * Collision.CellSize * 0.1f);
        }
    }

    /// <summary>Reads the zone's height fields, if the ATTR archive is there.</summary>
    private static CollisionShapes? LoadShapes(string actPath)
    {
        string? directory = Path.GetDirectoryName(actPath);
        if (directory is null) return null;

        foreach (string file in Directory.EnumerateFiles(directory, "*_ATTR.AMB"))
        {
            try
            {
                var archive = AmbArchive.Load(file);
                foreach (var entry in archive.Entries)
                {
                    if (!entry.Name.EndsWith(".DF", StringComparison.OrdinalIgnoreCase))
                        continue;
                    return CollisionShapes.Parse(archive.Read(entry).Span, 4096);
                }
            }
            catch (AmbException)
            {
                // A zone without usable shape data falls back to blocky ground.
            }
        }
        return null;
    }

    private void ExitStage()
    {
        Scheduler.DeleteGroup(SceneGroup);
        Stage = null;
        StageName = null;
        Collision = null;
        Player = null;
        Placements = [];
    }

    /// <summary>Runs one frame.</summary>
    public void Step()
    {
        Events.Step();
        Scheduler.Step();
        Frame++;
    }

    /// <summary>
    /// Resolves an act archive to the model archive its tile ids index.
    /// </summary>
    /// <remarks>
    /// <c>ZONE&lt;zone&gt;&lt;act&gt;[&lt;tileset&gt;]_MAP</c> maps to
    /// <c>ZONE&lt;zone&gt;[&lt;tileset&gt;]_M</c>. Zones with one shared tileset
    /// omit the letter, which is why the plain form is tried as a fallback.
    /// </remarks>
    public static string? FindTileset(string actPath)
    {
        string? directory = Path.GetDirectoryName(actPath);
        if (directory is null) return null;

        string name = Path.GetFileNameWithoutExtension(actPath);
        if (!name.StartsWith("ZONE", StringComparison.OrdinalIgnoreCase)) return null;

        string body = name[4..].Replace("_MAP", "", StringComparison.OrdinalIgnoreCase);
        if (body.Length < 2) return null;

        string tail = body[^1..];
        bool hasTilesetLetter = !char.IsDigit(tail[0]);
        string tileset = hasTilesetLetter ? tail : "";
        string zone = hasTilesetLetter ? body[..^2] : body[..^1];

        foreach (string candidate in new[] { $"ZONE{zone}{tileset}_M.AMB", $"ZONE{zone}_M.AMB" })
        {
            string path = Path.Combine(directory, candidate);
            if (File.Exists(path)) return path;
        }
        return null;
    }
}
