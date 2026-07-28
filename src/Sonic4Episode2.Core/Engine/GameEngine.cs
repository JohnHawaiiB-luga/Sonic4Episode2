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
    /// <remarks>
    /// <see cref="PriorityObject"/> is the engine's real value, read from the
    /// argument its spawn handlers pass to the shared object constructor. The map
    /// and camera priorities are still placeholders either side of it.
    /// </remarks>
    public const int PriorityMap = 0x1000;
    public const int PriorityObject = ObjectCatalog.Priority;
    public const int PriorityCamera = 0x3000;

    private readonly IContentSource _content;

    /// <summary>Runs against an installed copy of the game on disk.</summary>
    public GameEngine(string gameRoot) : this(new FileSystemContent(gameRoot)) { }

    /// <summary>
    /// Runs against any content source, which is how the mobile heads supply data
    /// out of an APK or bundle rather than a filesystem.
    /// </summary>
    public GameEngine(IContentSource content)
    {
        _content = content;
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

    /// <summary>Ring positions, which come from the stage's `.RG` file.</summary>
    public IReadOnlyList<Ring> Rings { get; private set; } = [];

    /// <summary>The mounted stage's rings and which have been taken.</summary>
    public RingField? RingField { get; private set; }

    /// <summary>Rings the player is carrying.</summary>
    public int RingCount { get; private set; }

    /// <summary>The mounted stage's springs.</summary>
    public Springs? Springs { get; private set; }

    /// <summary>The mounted stage's dash panels.</summary>
    public DashPanels? DashPanels { get; private set; }

    /// <summary>
    /// The object id of the act start marker.
    /// </summary>
    /// <remarks>
    /// Not a name — a structural identification. Across the 13 non-boss acts it
    /// is placed exactly once per act at a mean 3% of the act's width, in the
    /// playable band; nothing else has that shape. It is where the original game
    /// starts the player, and where this engine now does.
    /// </remarks>
    public const int StartMarkerId = 443;

    /// <summary>
    /// The object id of the goal panel, identified the same way: exactly once
    /// per act, at a mean 86% of the act's width, in 11 of 13 acts.
    /// </summary>
    public const int GoalPanelId = 520;

    /// <summary>Where the goal panel stands, in world units, if the act has one.</summary>
    public System.Numerics.Vector2? GoalPosition { get; private set; }

    /// <summary>Whether the player has crossed the goal.</summary>
    public bool ActClear { get; private set; }

    public string? StageName { get; private set; }
    public ulong Frame { get; private set; }

    /// <summary>Act archive the stage scene will mount, relative to the root.</summary>
    public string ActArchive { get; set; } = "G_ZONE1/MAP/ZONE11_MAP.AMB";

    /// <summary>
    /// Cell the player is dropped into, or null to use a point near the act's
    /// start.
    /// </summary>
    /// <remarks>
    /// The real start position comes from the stage data and is not identified
    /// yet, so this exists to put the player somewhere worth looking at while
    /// testing. Zone 1 Act 1's opening stretch is flat solid ground for forty
    /// cells, which is correct and extremely dull.
    /// </remarks>
    public int? SpawnCellX { get; set; }

    /// <summary>
    /// Row the drop starts from, or null to start near the top.
    /// </summary>
    /// <remarks>
    /// Needed because a stage's upper rows are not sky. Zone 1 Act 1 is solid
    /// masonry from row 0 to row 25 across its whole width — the castle wall
    /// behind the level — so a drop that starts at the top lands on the backdrop
    /// instead of the floor.
    /// </remarks>
    public int? SpawnCellY { get; set; }

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
        string actPath = ActArchive;
        var archive = AmbArchive.Parse(_content.Read(actPath));

        string? tilesetPath = FindTileset(actPath, _content);
        if (tilesetPath is null)
            throw new InvalidOperationException($"no tileset archive beside {actPath}");

        var assembler = new StageAssembler(AmbArchive.Parse(_content.Read(tilesetPath)));
        var batch = new StageBatch();
        var placements = new List<Placement>();
        var rings = new List<Ring>();
        int layers = 0;

        // Ground shapes and their angles live in the zone's ATTR archive.
        var (shapes, angles) = LoadShapes(actPath, _content);
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
            if (label.EndsWith(".RG", StringComparison.OrdinalIgnoreCase))
            {
                rings.AddRange(RingPlacements.Parse(archive.Read(entry).Span).Items);
                continue;
            }
            string? suffix = VisualLayerOf(label);
            if (suffix is null) continue;

            var grid = StageGrid.Parse(label, archive.Read(entry).Span);
            assembler.AddLayer(grid, suffix, batch);
            layers++;
        }

        if (attributeGrid is not null)
            Collision = CollisionMap.FromGrid(attributeGrid, shapes, angles);

        Stage = batch;
        Placements = placements;
        Rings = rings;
        RingField = new RingField(rings);
        RingCount = 0;
        Springs = new Springs(placements);
        DashPanels = new DashPanels(placements);
        StageName = NameOf(actPath);
        int identified = placements.Count(p => ObjectCatalog.IsKnown(p.ObjectId));
        Status = $"{assembler.TilesPlaced} tiles, {batch.VertexCount:N0} vertices, " +
                 $"{batch.TriangleCount:N0} triangles, " +
                 $"{layers} layers, {identified}/{placements.Count} placements identified, " +
                 $"{rings.Count} rings" +
                 (Collision?.HasShapes == true ? ", height fields" : ", blocky collision") +
                 (Collision?.HasAngles == true ? " with angles" : "");

        // The map is a task like anything else, so it obeys pause levels and is
        // torn down with the scene rather than by special-case code.
        Scheduler.Create("GM_MAP_MAIN", _ => { }, PriorityMap, group: SceneGroup);
        Scheduler.Create("GM_EVT_MGR", _ => Objects.Step(Scheduler.PauseLevel),
                         PriorityObject, group: SceneGroup);
        Scheduler.Create("GM_RING", _ => CollectRings(), PriorityObject,
                         group: SceneGroup);
        Scheduler.Create("GM_SPRING", _ => CheckSprings(), PriorityObject,
                         group: SceneGroup);
        Scheduler.Create("GM_DASHPANEL", _ => CheckDashPanels(), PriorityObject,
                         group: SceneGroup);
        Scheduler.Create("GM_GOAL", _ => CheckGoal(), PriorityObject,
                         group: SceneGroup);

        // The act's own start and goal, from their placements.
        float scale = Assets.PlayerPhysics.WorldPerPixel;
        Placement? start = placements.Where(p => p.ObjectId == StartMarkerId)
                                     .Cast<Placement?>().FirstOrDefault();
        Placement? goal = placements.Where(p => p.ObjectId == GoalPanelId)
                                    .Cast<Placement?>().FirstOrDefault();
        GoalPosition = goal is null ? null
            : new System.Numerics.Vector2(goal.Value.X * scale, -goal.Value.Y * scale);
        ActClear = false;

        if (Collision is not null)
        {
            Player = Objects.Add(new Player(Collision));
            // The real spawn is the act's start marker; the cell overrides exist
            // for debugging, and the fraction fallback for acts without one.
            float spawnX = SpawnCellX is not null ? SpawnCellX.Value * Collision.CellSize
                : start is not null ? start.Value.X * scale
                : Collision.Width * Collision.CellSize * 0.06f;
            float spawnY = SpawnCellY is not null ? -SpawnCellY.Value * Collision.CellSize
                : start is not null ? -start.Value.Y * scale + Collision.CellSize
                : -Collision.Height * Collision.CellSize * 0.1f;
            Player.PlaceOnGround(spawnX, spawnY);
        }
    }

    /// <summary>
    /// The layer a grid belongs to, or null when it is not one to draw.
    /// </summary>
    /// <remarks>
    /// An act ships sixteen grids and only seven of them are scenery. The
    /// <c>_ATTR_</c> pair is collision and must never be drawn, and the longest
    /// suffix has to win — <c>ZONE11_M1.MP</c> ends with <c>1.MP</c> but is the
    /// <c>_M1</c> layer, and testing <c>_M</c> first would put it at the wrong
    /// depth.
    /// </remarks>
    private static string? VisualLayerOf(string label)
    {
        if (label.Contains("_ATTR", StringComparison.OrdinalIgnoreCase)) return null;
        foreach (string suffix in StageAssembler.LayerOrder)
            if (label.EndsWith($"{suffix}.MP", StringComparison.OrdinalIgnoreCase))
                return suffix;
        return null;
    }

    /// <summary>
    /// Reads the zone's height fields and surface angles, if the ATTR archive is
    /// there. Either may be absent; the map degrades rather than failing.
    /// </summary>
    private static (CollisionShapes? Shapes, CollisionShapes? Angles) LoadShapes(
        string actPath, IContentSource content)
    {
        string directory = DirectoryOf(actPath);

        foreach (string file in content.List(directory, "_ATTR.AMB"))
        {
            try
            {
                var archive = AmbArchive.Parse(content.Read(file));
                CollisionShapes? shapes = null, angles = null;
                foreach (var entry in archive.Entries)
                {
                    if (entry.Name.EndsWith(".DF", StringComparison.OrdinalIgnoreCase))
                        shapes = CollisionShapes.Parse(archive.Read(entry).Span, 4096);
                    else if (entry.Name.EndsWith(".DI", StringComparison.OrdinalIgnoreCase))
                        angles = CollisionShapes.Parse(archive.Read(entry).Span,
                                                       CollisionShapes.CellsPerRecord);
                }
                if (shapes is not null) return (shapes, angles);
            }
            catch (AmbException)
            {
                // A zone without usable shape data falls back to blocky ground.
            }
        }
        return (null, null);
    }

    private void ExitStage()
    {
        Scheduler.DeleteGroup(SceneGroup);
        Stage = null;
        StageName = null;
        Collision = null;
        Player = null;
        Placements = [];
        Rings = [];
        RingField = null;
        RingCount = 0;
        Springs = null;
        DashPanels = null;
        GoalPosition = null;
        ActClear = false;
    }

    /// <summary>Clears the act when the player crosses the goal panel.</summary>
    /// <remarks>
    /// Crossing is passing the panel's X while within a screen of its height —
    /// the same left-to-right reading the placement statistics support. Vertical
    /// acts whose goal is elsewhere will need the real goal behaviour.
    /// </remarks>
    private void CheckGoal()
    {
        if (ActClear || GoalPosition is null || Player is null) return;
        var goal = GoalPosition.Value;
        if (Player.Position.X >= goal.X &&
            MathF.Abs(Player.Position.Y - goal.Y) < 64f * Assets.PlayerPhysics.WorldPerPixel)
        {
            ActClear = true;
            Status = $"ACT CLEAR - {StageName}, {RingCount} rings";
        }
    }

    /// <summary>Fires a dash panel under the player.</summary>
    private void CheckDashPanels()
    {
        if (DashPanels is null || Player is null) return;
        float? boost = DashPanels.Check(new System.Numerics.Vector2(
            Player.Position.X, Player.Position.Y));
        if (boost is not null)
            Player.DashBoost(boost.Value, Engine.DashPanels.NoFrictionFrames);
    }

    /// <summary>Fires a spring under the player.</summary>
    private void CheckSprings()
    {
        if (Springs is null || Player is null) return;
        float? impulse = Springs.Check(new System.Numerics.Vector2(
            Player.Position.X, Player.Position.Y));
        if (impulse is not null) Player.Bounce(impulse.Value);
    }

    /// <summary>Hands the player any ring it is standing in.</summary>
    private void CollectRings()
    {
        if (RingField is null || Player is null) return;
        RingCount += RingField.Collect(new System.Numerics.Vector2(
            Player.Position.X, Player.Position.Y));
        Player.TryGoSuper(RingCount);
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
    public static string? FindTileset(string actPath) =>
        FindTileset(actPath, new FileSystemContent(""));

    /// <inheritdoc cref="FindTileset(string)"/>
    public static string? FindTileset(string actPath, IContentSource content)
    {
        string directory = DirectoryOf(actPath);
        string name = NameOf(actPath);
        if (!name.StartsWith("ZONE", StringComparison.OrdinalIgnoreCase)) return null;

        string body = name[4..].Replace("_MAP", "", StringComparison.OrdinalIgnoreCase);
        if (body.Length < 2) return null;

        string tail = body[^1..];
        bool hasTilesetLetter = !char.IsDigit(tail[0]);
        string tileset = hasTilesetLetter ? tail : "";
        string zone = hasTilesetLetter ? body[..^2] : body[..^1];

        foreach (string candidate in new[] { $"ZONE{zone}{tileset}_M.AMB", $"ZONE{zone}_M.AMB" })
        {
            string path = directory.Length == 0 ? candidate : $"{directory}/{candidate}";
            if (content.Exists(path)) return path;
        }
        return null;
    }

    /// <summary>The directory part of a content path, or empty.</summary>
    /// <remarks>
    /// Content paths are always <c>/</c>-separated regardless of platform, so this
    /// does not use <see cref="Path"/> — on Windows that would also split on a
    /// backslash and quietly accept paths no content source can serve.
    /// </remarks>
    private static string DirectoryOf(string path)
    {
        int cut = path.LastIndexOf('/');
        return cut < 0 ? "" : path[..cut];
    }

    /// <summary>The file name without its extension.</summary>
    private static string NameOf(string path)
    {
        string name = path[(path.LastIndexOf('/') + 1)..];
        int dot = name.LastIndexOf('.');
        return dot < 0 ? name : name[..dot];
    }
}
