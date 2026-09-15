using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;
// The head speaks MonoGame's vector types; the engine's are only used
// through it, so resolve the clash in favour of the graphics ones here.
using Vector2 = Microsoft.Xna.Framework.Vector2;
using Vector3 = Microsoft.Xna.Framework.Vector3;

namespace Sonic4Episode2.Desktop;

/// <summary>
/// The first thing in this project that actually runs: a window showing a stage
/// assembled live from the game's own archives.
/// </summary>
/// <remarks>
/// This is a viewer, not the game. There is no player, no physics and no game
/// logic — it exists to prove the asset chain works end to end inside a real
/// graphics context rather than only in an offline rasteriser.
/// <para>
/// Arrow keys pan, Page Up/Down zoom, Escape quits.
/// </para>
/// </remarks>
public sealed class StageViewerGame : Game
{
    private readonly GraphicsDeviceManager _graphics;
    private readonly IInputSource? _input;
    private readonly IContentSource _content;
    private readonly string _actArchive;

    /// <summary>
    /// Scene ambient level, taken from the materials themselves.
    /// </summary>
    /// <remarks>
    /// Every material in the build carries an ambient RGBA, and 4,859 of the
    /// 9,767 use exactly this uniform grey — the commonest value by a wide
    /// margin, with black (2,792) next. Using it as the scene ambient keeps
    /// unlit faces at the level the artists authored instead of at black.
    /// </remarks>
    private const float StageAmbient = 0.30f;

    private BasicEffect _effect = null!;

    /// <summary>
    /// Our own stage material effect, or null when it could not be loaded.
    /// </summary>
    /// <remarks>
    /// Implements the material model recovered in <c>docs/ORACLES.md</c> —
    /// texture modulated by the material diffuse, over a scene ambient, lit per
    /// pixel by one parallel light. <see cref="_effect"/> stays for the sky,
    /// objects, rings and markers until this covers them too, so a failure here
    /// degrades to the previous renderer rather than to a black screen.
    /// </remarks>
    private Effect? _stageEffect;
    private readonly MaterialRenderStates _materialStates = new();
    private StageVertex[] _vertices = [];
    private readonly Dictionary<MaterialKey, int[]> _batches = [];

    /// <summary>
    /// The stage geometry, resident on the GPU.
    /// </summary>
    /// <remarks>
    /// The stage used to draw through <c>DrawUserIndexedPrimitives</c>, which
    /// re-uploads the vertex array it is handed on <b>every call</b>. At 8.5M
    /// vertices over 43 material batches that was hundreds of megabytes crossing
    /// the bus per frame, and it cost <b>5.5 seconds a frame</b> — the renderer
    /// was transfer-bound, not shader-bound. Uploading once and drawing from
    /// device buffers is the whole fix.
    /// </remarks>
    private VertexBuffer? _vertexBuffer;
    private IndexBuffer? _indexBuffer;

    /// <summary>
    /// Where each material's triangles sit inside <see cref="_indexBuffer"/>,
    /// broken into columns and <b>sorted by column</b>.
    /// </summary>
    /// <remarks>
    /// Sorting is what makes culling free at draw time: because a material's
    /// columns are laid out in ascending order inside the shared buffer, the
    /// whole visible column range is one contiguous span, so a culled draw is
    /// still a single <c>DrawIndexedPrimitives</c> — just a shorter one.
    /// </remarks>
    private readonly Dictionary<MaterialKey, ColumnSpan[]> _batchSpans = [];

    /// <summary>One column's triangles within a material's region of the buffer.</summary>
    private readonly record struct ColumnSpan(int Column, int Start, int Count);
    private readonly Dictionary<string, Texture2D> _textures = [];
    private StageBatch? _pending;
    private Texture2D _white = null!;
    private Texture2D _marker = null!;
    private Texture2D _ring = null!;
    private TileMesh? _ringMesh;
    /// <summary>
    /// One distinct object model's geometry, resident on the GPU.
    /// </summary>
    /// <remarks>
    /// Posing changes vertex positions but never counts, materials or triangle
    /// order, so the buffers and the per-material ranges are allocated once and
    /// only the vertex data is rewritten as the model animates.
    /// </remarks>
    private sealed class ObjectGeometry
    {
        public required DynamicVertexBuffer Vertices { get; init; }
        public required IndexBuffer Indices { get; init; }
        public required Dictionary<MaterialKey, (int Start, int Count)> Batches { get; init; }
        public required StageVertex[] Scratch { get; init; }

        /// <summary>
        /// The model's own X extent, measured from the geometry being uploaded.
        /// </summary>
        /// <remarks>
        /// Culling an instance on its origin alone needs a margin, and any fixed
        /// margin is a guess that is either wasteful or wrong. Measuring the real
        /// extent as the vertices go up costs one pass over data already in hand
        /// and makes the test exact: an instance is dropped only when its actual
        /// geometry falls outside the view. Recomputed per frame for animated
        /// models, since a pose can reach past the rest extent.
        /// </remarks>
        public float MinX { get; set; }
        public float MaxX { get; set; }
    }

    /// <summary>
    /// Each distinct object model's geometry, keyed by identity.
    /// </summary>
    /// <remarks>
    /// The act used to merge every placement into one array and rebuild it each
    /// frame — 153,726 vertices for Zone 1's 126 placements of 10 distinct models
    /// — then hand that array to <c>DrawUserIndexedPrimitives</c> once per
    /// material, which re-uploads it every call. Thirteen batches meant the same
    /// 153,726 vertices crossed the bus thirteen times a frame. Keeping one copy
    /// of each distinct model and drawing it per placement uploads about 12,000
    /// vertices instead, and only when the model actually animates.
    /// </remarks>
    private readonly Dictionary<LoadedObject, ObjectGeometry> _objectGeometry =
        new(ReferenceEqualityComparer.Instance);

    /// <summary>Where each distinct model stands, so instances draw in one group.</summary>
    private readonly Dictionary<LoadedObject, List<(float X, float Y)>> _placementsByObject =
        new(ReferenceEqualityComparer.Instance);
    private int _objectInstances;
    private StageVertex[] _skyVertices = [];
    private readonly Dictionary<MaterialKey, int[]> _skyBatches = [];
    private float _skyCenterY;
    private readonly List<BackgroundGeometry> _pcBackground = [];
    private sealed record BackgroundGeometry(StageVertex[] Vertices, int[] Indices,
        (MaterialKey Material, int Start, int Count)[] Runs, bool FollowCamera);
    private StageVertex[] _ringVertices = [];
    private readonly Dictionary<MaterialKey, int[]> _ringBatches = [];
    private int _ringsBuiltFor = -1;
    private bool _reportedCulling;

    /// <summary>
    /// Per-frame draw times, for measuring the renderer honestly.
    /// </summary>
    /// <remarks>
    /// Wall-clocking the whole process conflates load time with draw time and is
    /// hostage to whatever else the machine is doing — two identical runs came
    /// out 33% apart while another job was building. Timing <see cref="Draw"/>
    /// itself and reporting the <b>minimum</b> gives a figure that contention can
    /// only spoil upward, so the best sample is the closest to the truth.
    /// <para>
    /// Caveat worth keeping in mind: this measures CPU time in the method, not
    /// GPU completion, since draw calls are queued rather than finished when
    /// <see cref="Draw"/> returns. It is the right instrument for the CPU-side
    /// costs that dominate here and the wrong one for judging shader expense.
    /// </para>
    /// </remarks>
    private readonly List<double> _frameTimes = [];

    /// <summary>Per-frame update times, measured the same way.</summary>
    private readonly List<double> _updateTimes = [];

    /// <summary>
    /// Draw time split by phase, so the frame budget is attributed rather than
    /// assumed. Twice this session I moved cost from one place while believing it
    /// lived in another; measuring per phase is what stops a third time.
    /// </summary>
    private readonly Dictionary<string, double> _phaseTimes = [];
    private long _phaseMark;

    private void Phase(string name)
    {
        long now = System.Diagnostics.Stopwatch.GetTimestamp();
        if (_phaseMark != 0)
        {
            double ms = (now - _phaseMark) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
            _phaseTimes[name] = _phaseTimes.GetValueOrDefault(name) + ms;
        }
        _phaseMark = now;
    }
    private GameEngine _engine = null!;

    private Vector2 _camera;
    private float _zoom = 1f;
    private bool _followPlayer;
    private bool _tabHeld;
    private bool _restartHeld;
    private string _status = "";
    private int _shownRings = -1;
    private bool _shownRolling;
    private int _shownLives = int.MinValue;
    private bool _shownGameOver;

    /// <summary>
    /// When set, the viewer draws this many frames, writes a PNG and exits.
    /// </summary>
    /// <remarks>
    /// A real screenshot of the real renderer is the only honest way to show that
    /// the stage draws — an offline rasteriser proves the data decoded, not that
    /// the engine works. A few frames pass first so the camera settles and the
    /// player has landed.
    /// </remarks>
    public string? ScreenshotPath { get; set; }
    public bool PlaybackSmoke { get; set; }
    public bool RestartSmoke { get; set; }
    public int PlaybackSmokeExitCode { get; private set; } = 1;
    private bool IsSmoke => PlaybackSmoke || RestartSmoke;
    private const int SmokeUpdates = 180;
    private int _smokeUpdates;
    private int _smokeDraws;
    private bool _smokePlayerDrawn;
    private System.Numerics.Vector3 _smokeStart;
    private bool _smokeJumpIssued;
    private bool _smokeRose;
    private bool _smokeLanded;
    private bool _smokePendingDraw;
    private string? _smokeFailure;
    private bool _restartSmokeCaptured;
    private bool _restartSmokeDamageApplied;
    private bool _restartSmokeSawDead;
    private bool _restartSmokeRestarted;
    private ulong _restartSmokeInitialAttempt;
    private Player? _restartSmokeOldPlayer;
    private object? _restartSmokeOldStage;
    private object? _restartSmokeOldCollision;
    private TaskControlBlock[] _restartSmokeOldTasks = [];
    private int _restartSmokeInitialLives;
    private int _restartSmokeObservedLives;
    private int _restartSmokeLifeChanges;
    private int _restartSmokeInitialRings;
    private int _restartSmokeInitialRingFieldCount;
    private int _restartSmokeInitialItemBoxes;
    private int _restartSmokeInitialTaskCount;
    private int _restartSmokeInitialObjectCount;
    private int _restartSmokeTotalUpdates;
    private int _restartSmokeDeathUpdates;
    private int _restartSmokeLivesAfterRestart;
    private int _restartSmokeTaskCountAfterRestart;
    private int _restartSmokeObjectCountAfterRestart;
    private bool _restartSmokeOldPlayerDestroyed;
    private bool _restartSmokeOldTasksDeleted;
    private bool _restartSmokeStageRetained;
    private bool _restartSmokeCollisionRetained;
    private bool _restartSmokeNewPlayerAlive;
    private bool _restartSmokeRingsReset;
    private bool _restartSmokeStateReset;
    private bool _restartSmokeTaskCardinalityReset;
    private bool _restartSmokeLivesConsumedOnce;
    private bool _restartSmokeFadePositiveOpacityDrawn;
    private bool _restartSmokeFadeSampled;
    private bool _restartSmokeFadePassed;
    private float _restartSmokeFadeOpacity;
    private int _restartSmokeFadeSourcePixels;
    private int _restartSmokeFadeMismatchPixels;
    private long _restartSmokeFadeSourceRgbSum;
    private long _restartSmokeFadeResultRgbSum;

    /// <summary>Frames to run before the screenshot is taken.</summary>
    public int ScreenshotFrame { get; set; } =
        int.TryParse(Environment.GetEnvironmentVariable("SCREENSHOT_FRAME"), out int f) ? f : 30;

    /// <summary>Cell to drop the player into; see <see cref="GameEngine.SpawnCellX"/>.</summary>
    public int? SpawnCellX { get; set; }

    /// <inheritdoc cref="GameEngine.SpawnCellY"/>
    public int? SpawnCellY { get; set; }

    public IReadOnlyDictionary<string, StageSceneData>? NativeScenes { get; set; }

    private int _frames;

    /// <summary>
    /// Runs against an installed copy with keyboard input, which is the desktop
    /// case. The other constructor is what the mobile heads use.
    /// </summary>
    public StageViewerGame(string gameRoot, string actArchive)
        : this(new FileSystemContent(gameRoot), actArchive, null) { }

    public StageViewerGame(IContentSource content, string actArchive, IInputSource? input)
    {
        _content = content;
        _input = input;
        _actArchive = actArchive;
        _graphics = new GraphicsDeviceManager(this)
        {
            PreferredBackBufferWidth = 1280,
            PreferredBackBufferHeight = 720,
        };
        IsMouseVisible = true;
        Window.Title = "Sonic 4 Episode II";
    }

    protected override void Initialize()
    {
        LoadStage();
        base.Initialize();
    }

    /// <summary>Boots the engine and lets it mount the stage.</summary>
    /// <remarks>
    /// The head loads nothing itself any more. It creates the engine, steps it
    /// until the scene machine reaches a state with a stage mounted, then
    /// renders whatever the engine produced. Loading belongs to the stage scene.
    /// </remarks>
    private void LoadStage()
    {
        _engine = new GameEngine(_content)
        {
            ActArchive = _actArchive,
            SpawnCellX = SpawnCellX,
            SpawnCellY = SpawnCellY,
            NativeScenes = NativeScenes,
        };

        // The boot scene requests its own exit on entry, so a single step lands
        // in the stage scene with its archives mounted.
        _engine.Step();

        if (_engine.Stage is null)
            throw new InvalidOperationException("engine reached no stage state");

        var batch = _engine.Stage;
        _pending = batch;
        _status = _engine.Status;
        _followPlayer = _engine.Player is not null;

        Console.WriteLine($"scene '{_engine.Events.Current.Name}': {_status}");
        Console.WriteLine($"{_engine.Scheduler.Count} tasks: " +
                          string.Join(", ", _engine.Scheduler.Tasks.Select(t => t.Name)));
        if (_engine.Player is not null)
            Console.WriteLine($"player spawned at {_engine.Player.Position}");
        if (_engine.Collision is not null)
            Console.WriteLine($"collision {_engine.Collision.Width}x{_engine.Collision.Height} cells");

        _camera = new Vector2((batch.MinX + batch.MaxX) / 2f, (batch.MinY + batch.MaxY) / 2f);
        if (_followPlayer && _engine.Player is not null)
        {
            _camera = new Vector2(_engine.Player.Position.X, _engine.Player.Position.Y);
            _zoom = 1.6f;
        }
        else
        {
            float span = Math.Max(batch.MaxX - batch.MinX, 1f);
            _zoom = 1280f / span;
        }
    }

    private void BuildBuffers(StageBatch batch)
    {
        _vertices = new StageVertex[batch.VertexCount];
        for (int i = 0; i < batch.VertexCount; i++)
        {
            _vertices[i] = new StageVertex(
                new Vector3(batch.Positions[i * 3],
                            batch.Positions[i * 3 + 1],
                            batch.Positions[i * 3 + 2]),
                // The model's own normal, not a constant. Feeding every vertex
                // the same forward normal is what made the stage read flat.
                i * 3 + 2 < batch.Normals.Count
                    ? new Vector3(batch.Normals[i * 3],
                                  batch.Normals[i * 3 + 1],
                                  batch.Normals[i * 3 + 2])
                    : Vector3.Backward,
                StageVertex.ReadTextureCoordinate(batch.TexCoords, i),
                StageVertex.ReadColor(batch.Colors, i));
        }
        foreach (var pair in batch.IndicesByMaterial)
            _batches[pair.Key] = [.. pair.Value];

        int multi = _batches.Keys.Count(k => k.IsMultiTexture);
        int envTriangles = _batches.Where(p => p.Key.Environment is not null)
                                   .Sum(p => p.Value.Length / 3);
        Console.WriteLine($"stage: {_batches.Count} material batches, " +
                          $"{multi} multi-textured ({envTriangles:N0} reflective triangles)");

        UploadStageGeometry(batch);
    }

    /// <summary>
    /// Moves the stage onto the GPU once, so drawing it is a state change rather
    /// than a transfer.
    /// </summary>
    /// <remarks>
    /// Every batch's indices are concatenated into one buffer and each material
    /// remembers its span, so the whole act needs one vertex buffer, one index
    /// buffer and one <c>DrawIndexedPrimitives</c> per material. Indices are
    /// 32-bit because an act runs to millions of vertices, far past what 16-bit
    /// can address.
    /// </remarks>
    private void UploadStageGeometry(StageBatch batch)
    {
        _vertexBuffer?.Dispose();
        _indexBuffer?.Dispose();
        _vertexBuffer = null;
        _indexBuffer = null;
        _batchSpans.Clear();
        if (_vertices.Length == 0) return;

        int total = _batches.Values.Sum(v => v.Length);
        if (total == 0) return;

        var all = new int[total];
        var spans = new List<ColumnSpan>();
        int at = 0;
        foreach (var pair in _batches)
        {
            var indices = pair.Value;
            int triangles = indices.Length / 3;
            var columns = batch.ColumnsByMaterial.TryGetValue(pair.Key, out var c) &&
                          c.Count == triangles
                ? c
                : null;

            // Order this material's triangles by column, so the visible range
            // later resolves to one contiguous span. Without recorded columns
            // the material keeps its original order as a single span, which
            // simply means it is never culled.
            var order = new int[triangles];
            for (int i = 0; i < triangles; i++) order[i] = i;
            if (columns is not null)
            {
                var keys = new int[triangles];
                for (int i = 0; i < triangles; i++) keys[i] = columns[i];
                Array.Sort(keys, order);
            }

            spans.Clear();
            int spanStart = at;
            int spanColumn = triangles > 0 && columns is not null ? columns[order[0]] : 0;
            for (int i = 0; i < triangles; i++)
            {
                int t = order[i];
                int column = columns is not null ? columns[t] : 0;
                if (column != spanColumn)
                {
                    spans.Add(new ColumnSpan(spanColumn, spanStart, at - spanStart));
                    spanStart = at;
                    spanColumn = column;
                }
                all[at++] = indices[t * 3];
                all[at++] = indices[t * 3 + 1];
                all[at++] = indices[t * 3 + 2];
            }
            if (at > spanStart)
                spans.Add(new ColumnSpan(spanColumn, spanStart, at - spanStart));

            _batchSpans[pair.Key] = [.. spans];
        }

        try
        {
            _vertexBuffer = new VertexBuffer(GraphicsDevice,
                StageVertex.VertexDeclaration, _vertices.Length,
                BufferUsage.WriteOnly);
            _vertexBuffer.SetData(_vertices);

            _indexBuffer = new IndexBuffer(GraphicsDevice, IndexElementSize.ThirtyTwoBits,
                                           total, BufferUsage.WriteOnly);
            _indexBuffer.SetData(all);

            Console.WriteLine(
                $"stage geometry uploaded: {_vertices.Length:N0} vertices, " +
                $"{total / 3:N0} triangles resident");
        }
        catch (Exception ex)
        {
            // A device that cannot hold the act falls back to the streaming path
            // rather than failing to draw. Slow beats blank.
            _vertexBuffer?.Dispose();
            _indexBuffer?.Dispose();
            _vertexBuffer = null;
            _indexBuffer = null;
            _batchSpans.Clear();
            Console.WriteLine($"stage geometry stays on the CPU: {ex.Message}");
        }
    }

    /// <summary>
    /// Decodes every DDS in the zone's texture archives and uploads it.
    /// </summary>
    /// <remarks>
    /// Textures live in the zone's <c>_T</c>/<c>_TEX</c> archives rather than
    /// beside the models, so this sweeps the act's directory rather than
    /// resolving per model.
    /// </remarks>
    private void LoadTextures(string actPath)
    {
        int cut = actPath.LastIndexOf('/');
        string directory = cut < 0 ? "" : actPath[..cut];

        foreach (string file in _content.List(directory, "_T.AMB")
                                       .Concat(_content.List(directory, "_TEX.AMB")))
        {
            AmbArchive archive;
            try { archive = AmbArchive.Parse(_content.Read(file)); }
            catch (AmbException) { continue; }

            foreach (var entry in archive.Entries)
            {
                if (!entry.Name.EndsWith(".DDS", StringComparison.OrdinalIgnoreCase)) continue;
                string label = entry.Name.Replace('\\', '/');
                label = label[(label.LastIndexOf('/') + 1)..].ToUpperInvariant();
                if (_textures.ContainsKey(label)) continue;

                try
                {
                    var decoded = DdsTexture.Parse(archive.Read(entry).Span);
                    var texture = new Texture2D(GraphicsDevice, decoded.Width, decoded.Height);
                    texture.SetData(decoded.Pixels);
                    _textures[label] = texture;
                }
                catch (Exception ex) when (ex is DdsException or ArgumentException)
                {
                    // A texture that will not decode simply falls back to white.
                }
            }
        }
        Console.WriteLine($"{_textures.Count} textures loaded");
    }

    protected override void LoadContent()
    {
        // The engine's own material model (docs/ORACLES.md) is texture modulated
        // by a diffuse term, plus parallel lights over a scene ambient. This is
        // the first cut of that: one parallel light against the ambient level
        // the materials themselves carry. The remaining texture stages and the
        // real light parameters are still to come.
        _effect = new BasicEffect(GraphicsDevice)
        {
            VertexColorEnabled = true,
            TextureEnabled = true,
            LightingEnabled = true,
            // The game lights per pixel, not per vertex. Its own shaders say so:
            // u_LightSource appears in 676 pixel shaders against 19 vertex ones
            // (docs/ORACLES.md, CTAB census). Beat 64 guessed per-vertex.
            PreferPerPixelLighting = true,
            SpecularColor = Vector3.Zero,
        };
        _effect.DirectionalLight0.Enabled = true;
        // Down-forward, so the side-on faces a 2D stage presents catch light and
        // surfaces angled away from the camera fall off instead of matching.
        _effect.DirectionalLight0.Direction =
            Vector3.Normalize(new Vector3(-0.3f, -0.6f, -0.75f));
        _effect.DirectionalLight0.DiffuseColor = new Vector3(0.85f);
        _effect.DirectionalLight0.SpecularColor = Vector3.Zero;
        _effect.DirectionalLight1.Enabled = false;
        _effect.DirectionalLight2.Enabled = false;
        // MaterialAmbient 0.3 grey is the value 4,859 of the build's materials
        // carry - the commonest ambient by a wide margin.
        _effect.AmbientLightColor = new Vector3(StageAmbient);
        _effect.DiffuseColor = Vector3.One;
        try
        {
            using var stream = TitleContainer.OpenStream("Content/Stage.mgfx");
            using var bytes = new MemoryStream();
            stream.CopyTo(bytes);
            _stageEffect = new Effect(GraphicsDevice, bytes.ToArray());
        }
        catch (Exception ex)
        {
            throw new InvalidDataException("cannot load Content/Stage.mgfx", ex);
        }
        Console.WriteLine("stage effect: Stage.mgfx");
        if (IsSmoke)
        {
            MaterialBlendCheck.Run(GraphicsDevice,
                _stageEffect,
                _effect, SetBlend);
            MaterialStateCheck.Run(GraphicsDevice, _stageEffect, _materialStates);
        }

        _white = new Texture2D(GraphicsDevice, 1, 1);
        _white.SetData(new[] { Color.White });
        _marker = new Texture2D(GraphicsDevice, 1, 1);
        _marker.SetData(new[] { new Color(70, 130, 255) });
        _ring = new Texture2D(GraphicsDevice, 1, 1);
        _ring.SetData(new[] { new Color(255, 200, 40) });

        LoadTextures(_actArchive);
        LoadRingModel();
        LoadObjectModels();
        LoadPlayerModel();
        LoadBackground();
        if (_pending is not null)
        {
            BuildBuffers(_pending);
            _pending = null;
        }
    }

    /// <summary>
    /// A flat quad where the player is.
    /// </summary>
    /// <remarks>
    /// There is no character model yet: Sonic's mesh and motions are in the
    /// archives but nothing binds them to the player object. A marker is enough
    /// to see that the physics and camera behave.
    /// </remarks>
    private void DrawPlayerMarker()
    {
        if (_engine.Player is null) return;

        float x = _engine.Player.Position.X;
        float y = _engine.Player.Position.Y;
        float halfWidth = Player.Width / 2f;
        float height = Player.Height;
        const float z = 400f;   // in front of every stage layer

        var corners = new[]
        {
            new StageVertex(new Vector3(x - halfWidth, y, z), Vector3.Backward, Vector2.Zero),
            new StageVertex(new Vector3(x + halfWidth, y, z), Vector3.Backward, Vector2.Zero),
            new StageVertex(new Vector3(x - halfWidth, y + height, z), Vector3.Backward, Vector2.Zero),
            new StageVertex(new Vector3(x + halfWidth, y + height, z), Vector3.Backward, Vector2.Zero),
        };
        var indices = new[] { 0, 1, 2, 2, 1, 3 };

        _effect.Texture = _marker;
        foreach (var pass in _effect.CurrentTechnique.Passes)
        {
            pass.Apply();
            GraphicsDevice.DrawUserIndexedPrimitives(
                PrimitiveType.TriangleList, corners, 0, 4, indices, 0, 2);
        }
    }

    /// <summary>A player mesh and the motion driving it.</summary>
    private sealed record PlayerMotion(
        IReadOnlyList<MotionSampler> Channels, float Start, float End);

    private NnModel? _playerModel;          // SON_MODEL, the full skeleton
    private NnModel? _playerBallModel;      // SON_SPINMODEL, the rolled-up ball
    private readonly Dictionary<string, PlayerMotion> _playerMotions = [];
    private string _playerMotionName = "";
    private float _playerFrame;

    /// <summary>
    /// Loads Sonic's skinned model, ball model, textures and the handful of
    /// locomotion motions the viewer can drive from player state.
    /// </summary>
    /// <remarks>
    /// This is what the matrix palette was recovered for: the model's 99 palette
    /// slots and per-vertex-list bone subsets go through
    /// <see cref="MatrixPalette.Build"/> and <see cref="TileMesh.Skinned"/> every
    /// frame. Anything missing leaves the flat marker in place.
    /// </remarks>
    private void LoadPlayerModel()
    {
        try
        {
            var models = AmbArchive.Parse(_content.Read("G_COM/PLY/SON_MDL.AMB"));
            foreach (var entry in models.Entries)
            {
                if (entry.Name.EndsWith("SON_MODEL.ZNO", StringComparison.OrdinalIgnoreCase))
                    _playerModel = NnModel.Load(models.Read(entry));
                else if (entry.Name.EndsWith("SON_SPINMODEL.ZNO", StringComparison.OrdinalIgnoreCase))
                    _playerBallModel = NnModel.Load(models.Read(entry));
            }
            if (_playerModel is null) return;

            LoadTexturesFrom("G_COM/PLY/SON_TEX.AMB");

            var motions = AmbArchive.Parse(_content.Read("G_COM/PLY/SON_MTN.AMB"));
            foreach (string name in (string[])
                     ["SON_FWWAIT0_01", "SON_WALK", "SON_FW", "SON_RUN", "SON_SPIN01"])
            {
                foreach (var entry in motions.Entries)
                {
                    if (!entry.Name.EndsWith(name + ".ZNM", StringComparison.OrdinalIgnoreCase))
                        continue;
                    var raw = motions.Read(entry);
                    var motion = NnFile.Parse(raw).ReadMotion();
                    if (motion is null) break;
                    var (header, channels) = motion.Value;
                    var samplers = new List<MotionSampler>();
                    foreach (var channel in channels)
                    {
                        var sampler = MotionSampler.Decode(channel, raw.Span);
                        if (sampler is not null) samplers.Add(sampler);
                    }
                    _playerMotions[name] = new PlayerMotion(samplers, header.Start, header.End);
                    break;
                }
            }
            Console.WriteLine($"player model loaded: {_playerModel.Nodes.Count} nodes, " +
                              $"{_playerModel.Header.MatrixPaletteCount} palette slots, " +
                              $"{_playerMotions.Count} motions");
        }
        catch (Exception ex) when (ex is AmbException or NnException or DdsException)
        {
            _playerModel = null;    // the marker quad still draws
        }
    }

    /// <summary>Which motion the player's state asks for right now.</summary>
    private string DesiredPlayerMotion()
    {
        var player = _engine.Player!;
        if (player.Rolling || player.Charging || !player.OnGround) return "SON_SPIN01";
        float speed = MathF.Abs(player.Velocity.X);
        if (speed < 0.05f) return "SON_FWWAIT0_01";
        return speed < player.MaxSpeed * 0.85f ? "SON_WALK" : "SON_RUN";
    }

    /// <summary>
    /// The player as the game's own skinned model, posed by state.
    /// </summary>
    /// <remarks>
    /// The ball states use <c>SON_SPINMODEL</c>; everything else skins
    /// <c>SON_MODEL</c>. The model is authored in world units with its feet at
    /// the origin facing +Z, so it rotates a quarter turn about Y toward travel
    /// and translates to the player's position — no scale involved.
    /// </remarks>
    private void DrawPlayer()
    {
        if (_engine.Player is null) return;
        var motionName = _playerModel is null ? "" : DesiredPlayerMotion();
        bool ball = motionName == "SON_SPIN01";
        var model = ball ? _playerBallModel : _playerModel;
        if (model is null || !_playerMotions.TryGetValue(motionName, out var motion))
        {
            DrawPlayerMarker();
            return;
        }

        if (motionName != _playerMotionName)
        {
            _playerMotionName = motionName;
            _playerFrame = motion.Start;
        }
        _playerFrame += 1f;
        float span = MathF.Max(motion.End - motion.Start, 1f);
        float frame = motion.Start + ((IsSmoke ? _smokeUpdates : _playerFrame - motion.Start) % span);

        var world = AnimatedPose.World(model.Nodes, motion.Channels, frame);
        var mesh = TileMesh.Skinned(model, world);

        var player = _engine.Player;
        float yaw = player.FacingLeft ? -MathF.Tau / 4f : MathF.Tau / 4f;
        var pose = System.Numerics.Matrix4x4.CreateRotationY(yaw) *
                   System.Numerics.Matrix4x4.CreateTranslation(
                       player.Position.X, player.Position.Y, 400f);

        var vertices = new StageVertex[mesh.Positions.Length / 3];
        for (int i = 0; i < vertices.Length; i++)
        {
            var p = System.Numerics.Vector3.Transform(new System.Numerics.Vector3(
                mesh.Positions[i * 3], mesh.Positions[i * 3 + 1],
                mesh.Positions[i * 3 + 2]), pose);
            vertices[i] = new StageVertex(
                new Vector3(p.X, p.Y, p.Z), Vector3.Backward,
                StageVertex.ReadTextureCoordinate(mesh.TexCoords, i),
                StageVertex.ReadColor(mesh.Colors, i));
        }

        // Group triangles by material, the same shape StageBatch produces.
        var groups = new Dictionary<MaterialKey, List<int>>();
        for (int t = 0; t < mesh.TriangleMaterials.Length; t++)
        {
            var key = mesh.TriangleMaterials[t];
            if (!groups.TryGetValue(key, out var list)) groups[key] = list = [];
            list.Add(mesh.Indices[t * 3]);
            list.Add(mesh.Indices[t * 3 + 1]);
            list.Add(mesh.Indices[t * 3 + 2]);
        }

        foreach (var pair in groups)
        {
            SetBlend(pair.Key);
            _effect.Texture = TextureNamed(pair.Key.Base);
            var indices = pair.Value;
            foreach (var pass in _effect.CurrentTechnique.Passes)
            {
                pass.Apply();
                GraphicsDevice.DrawUserIndexedPrimitives(
                    PrimitiveType.TriangleList, vertices, 0, vertices.Length,
                    [.. indices], 0, indices.Count / 3);
                if (IsSmoke && vertices.Length > 0 && indices.Count >= 3)
                    _smokePlayerDrawn = true;
            }
        }
        GraphicsDevice.BlendState = BlendState.NonPremultiplied;
    }

    /// <summary>
    /// Loads the game's own ring model, so rings are rings rather than squares.
    /// </summary>
    /// <remarks>
    /// <c>RING.ZNO</c> is a single-node model with one vertex list, which is why
    /// it can go through the same <see cref="TileMesh"/> path the stage tiles use
    /// with no skinning involved. Sonic's own model has 109 nodes and needs the
    /// skeleton evaluated first, which is a separate job.
    /// </remarks>
    private void LoadRingModel()
    {
        try
        {
            var models = AmbArchive.Parse(_content.Read("G_COM/RING/RING_MDL.AMB"));
            int at = -1;
            for (int i = 0; i < models.Entries.Count; i++)
            {
                if (models.Entries[i].Name.EndsWith(".ZNO", StringComparison.OrdinalIgnoreCase))
                {
                    at = i;
                    break;
                }
            }
            if (at < 0) return;

            var model = NnModel.Load(models.Read(models.Entries[at]));
            if (model is null) return;
            _ringMesh = TileMesh.From(model);

            var textures = AmbArchive.Parse(_content.Read("G_COM/RING/RING_TEX.AMB"));
            foreach (var tex in textures.Entries)
            {
                if (!tex.Name.EndsWith(".DDS", StringComparison.OrdinalIgnoreCase)) continue;
                string label = tex.Name.Replace((char)92, '/');
                label = label[(label.LastIndexOf('/') + 1)..].ToUpperInvariant();
                if (_textures.ContainsKey(label)) continue;
                var decoded = DdsTexture.Parse(textures.Read(tex).Span);
                var texture = new Texture2D(GraphicsDevice, decoded.Width, decoded.Height);
                texture.SetData(decoded.Pixels);
                _textures[label] = texture;
            }
        }
        catch (Exception ex) when (ex is AmbException or NnException or DdsException)
        {
            // Without the model the flat markers still draw, so this is a
            // downgrade rather than a failure.
            _ringMesh = null;
        }
    }

    /// <summary>
    /// Instances a model at every placement whose object resolves to one.
    /// </summary>
    /// <remarks>
    /// Only 11 of the 45 recovered object names resolve to an archive so far, so
    /// this draws springs, jet walls and their kin and leaves the rest as
    /// nothing — honestly absent rather than guessed at. The batch is built once:
    /// placements do not move.
    /// <para>
    /// Placement anchors are unknown, so each model sits centred on its
    /// placement point. Wrong for objects anchored at their base, but visibly so,
    /// which is what a first pass should be.
    /// </para>
    /// </remarks>
    /// <summary>A model and the animation to play on it, loaded once.</summary>
    private sealed record LoadedObject(
        NnModel? Model, TileMesh? Rest,
        IReadOnlyList<MotionSampler> Channels, float Start, float End);

    private readonly List<(LoadedObject Object, float X, float Y)> _objectPlacements = [];
    private bool _objectsAnimate;

    private void LoadObjectModels()
    {
        int cut = _actArchive.IndexOf('/');
        string zone = cut < 0 ? "" : _actArchive[..cut];
        string[] roots = [$"{zone}/GMK", "G_COM/GMK"];
        var archives = roots.SelectMany(r => _content.List(r, "_MDL.AMB")).ToArray();

        var loaded = new Dictionary<string, LoadedObject?>();
        _objectPlacements.Clear();
        _placementsByObject.Clear();
        _objectInstances = 0;
        _objectsAnimate = false;

        foreach (var placement in _engine.Placements)
        {
            // Try the engine class first (679 ids carry one) then the scraped
            // asset name (only 116, but sometimes the truer archive spelling).
            // Resolve only returns a confirmed match, so trying both can only add
            // correctly-resolved models, never a wrong one.
            string? archive = null;
            foreach (string? candidate in new[]
                     { ObjectCatalog.ClassOf(placement.ObjectId),
                       ObjectCatalog.NameOf(placement.ObjectId) })
            {
                if (candidate is null) continue;
                archive = ObjectModels.Resolve(candidate, archives);
                if (archive is not null) break;
            }
            if (archive is null) continue;

            if (!loaded.TryGetValue(archive, out var obj))
            {
                obj = LoadObject(archive);
                loaded[archive] = obj;
                if (obj?.Rest is not null)
                    LoadTexturesFrom(ObjectModels.TexturesFor(archive));
            }
            if (obj?.Rest is null) continue;

            float scale = PlayerPhysics.WorldPerPixel;
            float px = placement.X * scale, py = -placement.Y * scale;
            _objectPlacements.Add((obj, px, py));
            if (!_placementsByObject.TryGetValue(obj, out var places))
                _placementsByObject[obj] = places = [];
            places.Add((px, py));
            _objectInstances++;
            if (obj.Channels.Count > 0) _objectsAnimate = true;
        }

        BuildObjectBuffers(0f);
        Console.WriteLine($"{_objectInstances} object models placed " +
                          $"({loaded.Count(o => o.Value?.Rest is not null)} distinct, " +
                          $"{(_objectsAnimate ? "animated" : "static")})");
    }

    /// <summary>Rebuilds the object geometry at an animation frame.</summary>
    /// <remarks>
    /// Rigid models pose by transforming each mesh set by its node's world matrix
    /// at the frame; models without a motion stay at rest. Called once at load for
    /// a static act, and every frame for an animated one.
    /// </remarks>
    private int _itemBoxesRemaining = -1;

    private void BuildObjectBuffers(float frame)
    {
        _itemBoxesRemaining = _engine.ItemBoxes?.Remaining ?? -1;

        foreach (var (obj, places) in _placementsByObject)
        {
            if (places.Count == 0) continue;
            bool animates = obj.Model is not null && obj.Channels.Count > 0;

            // A static model's vertices never change, so it is uploaded once and
            // then left alone.
            if (_objectGeometry.TryGetValue(obj, out var geometry) && !animates) continue;

            TileMesh mesh = obj.Rest!;
            if (animates)
            {
                float f = obj.Start + (frame % MathF.Max(obj.End - obj.Start, 1f));
                var world = AnimatedPose.World(obj.Model!.Nodes, obj.Channels, f);
                mesh = TileMesh.Posed(obj.Model, world);
            }

            geometry ??= CreateObjectGeometry(obj, mesh);
            if (geometry is null) continue;

            var scratch = geometry.Scratch;
            int count = Math.Min(scratch.Length, mesh.Positions.Length / 3);
            float minX = float.MaxValue, maxX = float.MinValue;
            for (int i = 0; i < count; i++)
            {
                float x = mesh.Positions[i * 3];
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                scratch[i] = new StageVertex(
                    new Vector3(x, mesh.Positions[i * 3 + 1], mesh.Positions[i * 3 + 2]),
                    Vector3.Backward,
                    StageVertex.ReadTextureCoordinate(mesh.TexCoords, i),
                    StageVertex.ReadColor(mesh.Colors, i));
            }
            geometry.MinX = count > 0 ? minX : 0f;
            geometry.MaxX = count > 0 ? maxX : 0f;
            geometry.Vertices.SetData(scratch, 0, count, SetDataOptions.Discard);
        }
    }

    /// <summary>
    /// Allocates one model's device buffers and works out where each material's
    /// triangles sit inside them.
    /// </summary>
    /// <remarks>
    /// The index buffer is written once: posing moves vertices but never changes
    /// the material a triangle belongs to or the order they come in, so the
    /// ranges stay valid for every frame after.
    /// </remarks>
    private ObjectGeometry? CreateObjectGeometry(LoadedObject obj, TileMesh mesh)
    {
        int vertexCount = mesh.Positions.Length / 3;
        if (vertexCount == 0 || mesh.Indices.Length == 0) return null;

        var byMaterial = new Dictionary<MaterialKey, List<int>>();
        for (int t = 0; t < mesh.TriangleMaterials.Length; t++)
        {
            var key = mesh.TriangleMaterials[t];
            if (!byMaterial.TryGetValue(key, out var list)) byMaterial[key] = list = [];
            list.Add(mesh.Indices[t * 3]);
            list.Add(mesh.Indices[t * 3 + 1]);
            list.Add(mesh.Indices[t * 3 + 2]);
        }

        var indices = new int[byMaterial.Values.Sum(v => v.Count)];
        var ranges = new Dictionary<MaterialKey, (int Start, int Count)>();
        int at = 0;
        foreach (var (key, list) in byMaterial)
        {
            list.CopyTo(indices, at);
            ranges[key] = (at, list.Count);
            at += list.Count;
        }

        try
        {
            var vertices = new DynamicVertexBuffer(GraphicsDevice,
                StageVertex.VertexDeclaration, vertexCount,
                BufferUsage.WriteOnly);
            var indexBuffer = new IndexBuffer(GraphicsDevice, IndexElementSize.ThirtyTwoBits,
                                              indices.Length, BufferUsage.WriteOnly);
            indexBuffer.SetData(indices);

            var geometry = new ObjectGeometry
            {
                Vertices = vertices,
                Indices = indexBuffer,
                Batches = ranges,
                Scratch = new StageVertex[vertexCount],
            };
            _objectGeometry[obj] = geometry;
            return geometry;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"object geometry not uploaded: {ex.Message}");
            return null;
        }
    }

    /// <summary>Loads a model and, if there is one beside it, its first motion.</summary>
    private LoadedObject? LoadObject(string archivePath)
    {
        NnModel? model = LoadModel(archivePath);
        if (model is null) return null;
        var rest = TileMesh.From(model);

        // Motions live in the _MTN archive under the same stem.
        string mtnPath = archivePath[..^"_MDL.AMB".Length] + "_MTN.AMB";
        var channels = new List<MotionSampler>();
        float start = 0f, end = 1f;
        if (_content.Exists(mtnPath))
        {
            try
            {
                var mtn = AmbArchive.Parse(_content.Read(mtnPath));
                foreach (var entry in mtn.Entries)
                {
                    if (!entry.Name.EndsWith(".ZNM", StringComparison.OrdinalIgnoreCase)) continue;
                    var raw = mtn.Read(entry);
                    var motion = NnFile.Parse(raw).ReadMotion();
                    if (motion is null) continue;
                    var (m, headers) = motion.Value;
                    foreach (var header in headers)
                    {
                        var sampler = MotionSampler.Decode(header, raw.Span);
                        if (sampler is not null) channels.Add(sampler);
                    }
                    start = m.Start;
                    end = m.End;
                    break;   // the first motion is the idle/loop for a gimmick
                }
            }
            catch (Exception ex) when (ex is AmbException or NnException) { }
        }
        return new LoadedObject(model, rest, channels, start, end);
    }

    private NnModel? LoadModel(string archivePath)
    {
        try
        {
            var archive = AmbArchive.Parse(_content.Read(archivePath));
            foreach (var entry in archive.Entries)
            {
                if (!entry.Name.EndsWith(".ZNO", StringComparison.OrdinalIgnoreCase)) continue;
                var model = NnModel.Load(archive.Read(entry));
                if (model is not null && model.MeshSets.Count > 0) return model;
            }
        }
        catch (Exception ex) when (ex is AmbException or NnException) { }
        return null;
    }

    /// <summary>The first drawable model in an archive, or null.</summary>
    /// <summary>Uploads every texture in an archive that is not already loaded.</summary>
    private void LoadTexturesFrom(string archivePath)
    {
        if (!_content.Exists(archivePath)) return;
        try
        {
            var archive = AmbArchive.Parse(_content.Read(archivePath));
            foreach (var entry in archive.Entries)
            {
                if (!entry.Name.EndsWith(".DDS", StringComparison.OrdinalIgnoreCase)) continue;
                string label = entry.Name.Replace((char)92, '/');
                label = label[(label.LastIndexOf('/') + 1)..].ToUpperInvariant();
                if (_textures.ContainsKey(label)) continue;
                try
                {
                    var decoded = DdsTexture.Parse(archive.Read(entry).Span);
                    var texture = new Texture2D(GraphicsDevice, decoded.Width, decoded.Height);
                    texture.SetData(decoded.Pixels);
                    _textures[label] = texture;
                }
                catch (Exception ex) when (ex is DdsException or ArgumentException) { }
            }
        }
        catch (AmbException) { }
    }

    /// <summary>
    /// Loads the zone's far background — the sky, distant scenery and clouds.
    /// </summary>
    /// <remarks>
    /// This is what fills the black void behind the level. The models live in a
    /// nested <c>MAPFAR</c> archive per zone; they draw once, deep, centred on the
    /// middle of the stage. A proper background scrolls with parallax against the
    /// camera, which is a later refinement — drawing it at all is the point here.
    /// </remarks>
    private void LoadBackground()
    {
        if (NativeScenes is not null)
        {
            LoadPcBackground();
            return;
        }
        int cut = _actArchive.IndexOf('/');
        string zone = cut < 0 ? "" : _actArchive[..cut];
        // e.g. G_ZONE1 -> G_ZONE1/MAPFAR/EP2_MAPFAR_ZONE1.AMB
        string tag = zone.StartsWith("G_ZONE", StringComparison.OrdinalIgnoreCase)
            ? zone["G_".Length..] : zone;
        string path = $"{zone}/MAPFAR/EP2_MAPFAR_{tag}.AMB";
        if (!_content.Exists(path)) return;

        try
        {
            var outer = AmbArchive.Parse(_content.Read(path));
            AmbArchive? models = null, textures = null;
            foreach (var entry in outer.Entries)
            {
                if (entry.Name.EndsWith("_MDL.AMB", StringComparison.OrdinalIgnoreCase))
                    models = outer.OpenNested(entry);
                else if (entry.Name.EndsWith("_TEX.AMB", StringComparison.OrdinalIgnoreCase))
                    textures = outer.OpenNested(entry);
            }
            if (models is null) return;

            if (textures is not null)
                foreach (var entry in textures.Entries)
                {
                    if (!entry.Name.EndsWith(".DDS", StringComparison.OrdinalIgnoreCase)) continue;
                    string label = entry.Name.Replace((char)92, '/');
                    label = label[(label.LastIndexOf('/') + 1)..].ToUpperInvariant();
                    if (_textures.ContainsKey(label)) continue;
                    try
                    {
                        var decoded = DdsTexture.Parse(textures.Read(entry).Span);
                        var tex = new Texture2D(GraphicsDevice, decoded.Width, decoded.Height);
                        tex.SetData(decoded.Pixels);
                        _textures[label] = tex;
                    }
                    catch (Exception ex) when (ex is DdsException or ArgumentException) { }
                }

            // The sky, distant scenery and clouds. Each model is authored around a
            // shared origin - the vertical stack (clouds high, ground low) is in
            // its centre offset, which TileMesh.From strips - so re-add that
            // offset to keep the pieces in their authored relationship.
            var loaded = new List<(NnModel Model, TileMesh Mesh)>();
            foreach (var entry in models.Entries)
            {
                if (!entry.Name.EndsWith(".ZNO", StringComparison.OrdinalIgnoreCase)) continue;
                var model = NnModel.Load(models.Read(entry));
                if (model is null || model.MeshSets.Count == 0) continue;
                loaded.Add((model, TileMesh.From(model)));
            }
            if (loaded.Count == 0) return;

            float refX = loaded.Average(l => l.Model.Header.CenterX);
            float refY = loaded.Average(l => l.Model.Header.CenterY);

            var batch = new StageBatch();
            float depth = -900f;
            foreach (var (model, mesh) in loaded)
            {
                batch.Add(mesh, model.Header.CenterX - refX,
                          -(model.Header.CenterY - refY), depth);
                depth += 20f;
            }

            // The background is anchored so its bottom sits near the top of the
            // stage - the sky belongs above the level, not through it.
            _skyCenterY = _engine.Stage!.MaxY - (batch.MaxY - batch.MinY) * 0.25f;

            _skyVertices = new StageVertex[batch.VertexCount];
            for (int i = 0; i < _skyVertices.Length; i++)
                _skyVertices[i] = new StageVertex(
                    new Vector3(batch.Positions[i * 3], batch.Positions[i * 3 + 1],
                                batch.Positions[i * 3 + 2]),
                    Vector3.Backward,
                    StageVertex.ReadTextureCoordinate(batch.TexCoords, i),
                    StageVertex.ReadColor(batch.Colors, i));
            foreach (var pair in batch.IndicesByMaterial)
                _skyBatches[pair.Key] = [.. pair.Value];

            Console.WriteLine($"background loaded: {_skyVertices.Length:N0} vertices");
        }
        catch (Exception ex) when (ex is AmbException or NnException) { }
    }

    private void LoadPcBackground()
    {
        var bytes = _content.Read(MapFarCamera.FirstActArchive);
        MapFarCamera.ValidateFirstAct(bytes, _content.Read(MapFarCamera.SettingsArchive));
        var outer = AmbArchive.Parse(bytes);
        var models = outer.OpenNested(outer.Entries.Single(e =>
            e.Name.EndsWith("_MDL.AMB", StringComparison.OrdinalIgnoreCase)));
        var textures = outer.OpenNested(outer.Entries.Single(e =>
            e.Name.EndsWith("_TEX.AMB", StringComparison.OrdinalIgnoreCase)));
        foreach (var entry in textures.Entries)
        {
            if (!entry.Name.EndsWith(".DDS", StringComparison.OrdinalIgnoreCase)) continue;
            string label = entry.Name.Replace('\\', '/');
            label = label[(label.LastIndexOf('/') + 1)..].ToUpperInvariant();
            if (_textures.ContainsKey(label)) continue;
            var decoded = DdsTexture.Parse(textures.Read(entry).Span);
            var texture = new Texture2D(GraphicsDevice, decoded.Width, decoded.Height);
            texture.SetData(decoded.Pixels);
            _textures[label] = texture;
        }
        foreach (int index in new[] { 0, 1, 3, 2 })
        {
            var model = NnModel.Load(models.Read(models.Entries[index]))
                ?? throw new InvalidDataException("unreadable first-act background model");
            var mesh = TileMesh.ForRigidModel(model);
            var vertices = new StageVertex[mesh.Positions.Length / 3];
            for (int i = 0; i < vertices.Length; i++)
            {
                int at = i * 3;
                var normal = mesh.Normals.Length == mesh.Positions.Length
                    ? new Vector3(mesh.Normals[at], mesh.Normals[at + 1], mesh.Normals[at + 2])
                    : Vector3.Backward;
                vertices[i] = new StageVertex(
                    new Vector3(mesh.Positions[at], mesh.Positions[at + 1], mesh.Positions[at + 2]),
                    normal, StageVertex.ReadTextureCoordinate(mesh.TexCoords, i),
                    StageVertex.ReadColor(mesh.Colors, i));
            }
            var runs = new List<(MaterialKey Material, int Start, int Count)>();
            for (int start = 0; start < mesh.TriangleMaterials.Length;)
            {
                var material = mesh.TriangleMaterials[start];
                int end = start + 1;
                while (end < mesh.TriangleMaterials.Length && mesh.TriangleMaterials[end] == material) end++;
                runs.Add((material, start * 3, (end - start) * 3));
                start = end;
            }
            _pcBackground.Add(new BackgroundGeometry(vertices, mesh.Indices, [.. runs], index is 2 or 3));
        }
        Console.WriteLine($"PC background loaded: {_pcBackground.Sum(g => g.Vertices.Length):N0} vertices, {_pcBackground.Count} rigid models");
    }

    private void DrawPcBackground()
    {
        var camera = MapFarCamera.FirstActPosition(new System.Numerics.Vector2(
            _camera.X / StageSceneData.DisplayScale, _camera.Y / StageSceneData.DisplayScale));
        var offset = MapFarCamera.FirstActFollowOffset(camera);
        var view = Matrix.CreateLookAt(new Vector3(camera.X, camera.Y, camera.Z),
                                      new Vector3(camera.X, camera.Y, 0), Vector3.Up);
        var projection = Matrix.CreatePerspectiveFieldOfView(MapFarCamera.FirstActFieldOfView,
            GraphicsDevice.Viewport.Width / (float)GraphicsDevice.Viewport.Height,
            MapFarCamera.NearPlane, MapFarCamera.FarPlane);
        var effect = _stageEffect!;
        effect.CurrentTechnique = effect.Techniques["StageTechnique"];
        effect.Parameters["MaterialAmbient"].SetValue(new Vector3(StageAmbient));
        effect.Parameters["LightDirection"].SetValue(_effect.DirectionalLight0.Direction);
        effect.Parameters["LightDiffuse"].SetValue(_effect.DirectionalLight0.DiffuseColor);
        foreach (var geometry in _pcBackground)
        {
            var world = geometry.FollowCamera
                ? Matrix.CreateTranslation(offset.X, offset.Y, offset.Z) : Matrix.Identity;
            effect.Parameters["WorldViewProjection"].SetValue(world * view * projection);
            foreach (var run in geometry.Runs)
            {
                _materialStates.Apply(GraphicsDevice, run.Material);
                MaterialRenderStates.ConfigureEffect(effect, run.Material);
                foreach (var pass in effect.CurrentTechnique.Passes)
                {
                    pass.Apply();
                    GraphicsDevice.Textures[0] = TextureNamed(run.Material.Base);
                    GraphicsDevice.SamplerStates[0] = SamplerState.LinearWrap;
                    GraphicsDevice.DrawUserIndexedPrimitives(PrimitiveType.TriangleList,
                        geometry.Vertices, 0, geometry.Vertices.Length,
                        geometry.Indices, run.Start, run.Count / 3);
                }
            }
        }
        // The stage uses a separate projection, so its depth values cannot share this pass.
        GraphicsDevice.Clear(ClearOptions.DepthBuffer, Color.Transparent, 1f, 0);
    }

    private void DrawBackground()
    {
        if (_pcBackground.Count > 0)
        {
            DrawPcBackground();
            return;
        }
        if (_skyVertices.Length == 0) return;

        // Camera-locked with parallax: the background follows the camera but drifts
        // slower, so it reads as far away. The horizontal lock keeps it filling the
        // view; the vertical anchor keeps the sky above the level.
        float px = _camera.X * 0.85f;
        float py = _skyCenterY + _camera.Y * 0.15f;
        var effect = _stageEffect!;
        effect.CurrentTechnique = effect.Techniques["StageTechnique"];
        effect.Parameters["WorldViewProjection"].SetValue(
            Matrix.CreateTranslation(px, py, 0f) * _effect.View * _effect.Projection);
        effect.Parameters["MaterialAmbient"].SetValue(new Vector3(StageAmbient));
        effect.Parameters["LightDirection"].SetValue(_effect.DirectionalLight0.Direction);
        effect.Parameters["LightDiffuse"].SetValue(_effect.DirectionalLight0.DiffuseColor);

        foreach (var pair in _skyBatches)
        {
            _materialStates.Apply(GraphicsDevice, pair.Key);
            MaterialRenderStates.ConfigureEffect(effect, pair.Key);
            foreach (var pass in effect.CurrentTechnique.Passes)
            {
                pass.Apply();
                GraphicsDevice.Textures[0] = TextureNamed(pair.Key.Base);
                GraphicsDevice.DrawUserIndexedPrimitives(
                    PrimitiveType.TriangleList, _skyVertices, 0, _skyVertices.Length,
                    pair.Value, 0, pair.Value.Length / 3);
            }
        }
    }

    /// <summary>
    /// Sets the blend state for a batch key — additive for glow materials,
    /// ordinary transparency otherwise.
    /// </summary>
    private void SetBlend(MaterialKey key) =>
        GraphicsDevice.BlendState = key.IsAdditive
            ? BlendState.Additive : BlendState.NonPremultiplied;

    /// <summary>
    /// The uploaded texture of that name, or a white pixel when it is missing.
    /// </summary>
    /// <remarks>
    /// White rather than nothing: an absent texture then modulates to the
    /// material colour instead of drawing the mesh black, which is what a missing
    /// environment map should look like.
    /// </remarks>
    private Texture2D TextureNamed(string? name) =>
        name is not null && _textures.TryGetValue(name.ToUpperInvariant(), out var t)
            ? t : _white;

    /// <summary>
    /// The slice of a material's triangles lying in a column range, or null when
    /// none of it does.
    /// </summary>
    /// <remarks>
    /// One span rather than several because <see cref="_batchSpans"/> is sorted
    /// by column, so everything between the first and last visible column is
    /// already adjacent in the index buffer. Culling therefore costs a draw
    /// nothing — it only shortens one.
    /// </remarks>
    private static (int Start, int Count)? VisibleSpan(
        ColumnSpan[] spans, int minColumn, int maxColumn)
    {
        int start = -1, end = 0;
        foreach (var span in spans)
        {
            if (span.Column < minColumn) continue;
            if (span.Column > maxColumn) break;
            if (start < 0) start = span.Start;
            end = span.Start + span.Count;
        }
        return start < 0 ? null : (start, end - start);
    }

    /// <summary>Draws the placed object models.</summary>
    private void DrawObjects(float minX, float maxX)
    {
        if (_objectGeometry.Count == 0) return;

        // A broken item box stops being drawn. Positions match exactly: the
        // viewer and ItemBoxes derive world coordinates the same way.
        HashSet<(float, float)>? broken = null;
        if (_engine.ItemBoxes is { } boxes && boxes.Remaining != boxes.Count)
        {
            broken = [];
            for (int i = 0; i < boxes.Count; i++)
                if (boxes.IsBroken(i))
                {
                    var p = boxes.PositionOf(i);
                    broken.Add((p.X, p.Y));
                }
        }

        foreach (var (obj, geometry) in _objectGeometry)
        {
            if (!_placementsByObject.TryGetValue(obj, out var places)) continue;

            GraphicsDevice.SetVertexBuffer(geometry.Vertices);
            GraphicsDevice.Indices = geometry.Indices;

            foreach (var (key, range) in geometry.Batches)
            {
                SetBlend(key);
                _effect.Texture = TextureNamed(key.Base);

                foreach (var (x, y) in places)
                {
                    // Off-screen instances are skipped outright. Unlike the stage,
                    // where culling only shortens one draw, here it removes whole
                    // draw calls — Zone F places 200 objects and the camera sees a
                    // handful. The test uses the model's measured extent, so it
                    // drops an instance only when its real geometry is outside the
                    // view rather than when a padded origin guess says so.
                    if (x + geometry.MaxX < minX || x + geometry.MinX > maxX) continue;
                    if (broken is not null && broken.Contains((x, y))) continue;

                    _effect.World = Matrix.CreateTranslation(x, y, 385f);
                    foreach (var pass in _effect.CurrentTechnique.Passes)
                    {
                        pass.Apply();
                        GraphicsDevice.DrawIndexedPrimitives(
                            PrimitiveType.TriangleList, 0, range.Start, range.Count / 3);
                    }
                }
            }
        }

        _effect.World = Matrix.Identity;
        GraphicsDevice.SetVertexBuffer(null);
        GraphicsDevice.Indices = null;
    }

    /// <summary>
    /// Rebuilds the ring geometry, one instance of the model per ring still on
    /// the field.
    /// </summary>
    /// <remarks>
    /// Only when the count changes, which is a handful of times a second at most.
    /// Rebuilding beats tracking per-ring index ranges for something this cheap.
    /// </remarks>
    private void BuildRingBuffers()
    {
        var field = _engine.RingField;
        if (_ringMesh is null || field is null) return;

        _ringsBuiltFor = field.Collected;
        _ringBatches.Clear();

        var batch = new StageBatch();
        for (int i = 0; i < field.Count; i++)
        {
            if (field.IsTaken(i)) continue;
            var at = field.WorldPosition(i);
            batch.Add(_ringMesh, at.X, at.Y, 390f);
        }

        _ringVertices = new StageVertex[batch.VertexCount];
        for (int i = 0; i < _ringVertices.Length; i++)
        {
            _ringVertices[i] = new StageVertex(
                new Vector3(batch.Positions[i * 3],
                            batch.Positions[i * 3 + 1],
                            batch.Positions[i * 3 + 2]),
                Vector3.Backward,
                StageVertex.ReadTextureCoordinate(batch.TexCoords, i),
                StageVertex.ReadColor(batch.Colors, i));
        }
        foreach (var pair in batch.IndicesByMaterial)
            _ringBatches[pair.Key] = [.. pair.Value];
    }

    /// <summary>
    /// A quad per uncollected ring.
    /// </summary>
    /// <remarks>
    /// Rings come from the stage's own <c>.RG</c> file, so what this draws is the
    /// original layout rather than anything invented. Only the ones still on the
    /// field are drawn, which makes collection visible.
    /// </remarks>
    private void DrawRings()
    {
        var field = _engine.RingField;
        if (field is null || field.Remaining == 0) return;

        if (_ringMesh is not null)
        {
            if (_ringsBuiltFor != field.Collected) BuildRingBuffers();
            if (_ringVertices.Length == 0) return;

            foreach (var pair in _ringBatches)
            {
                SetBlend(pair.Key);
                _effect.Texture = _textures.TryGetValue(
                    (pair.Key.Base ?? "").ToUpperInvariant(), out var texture)
                    ? texture
                    : _ring;
                foreach (var pass in _effect.CurrentTechnique.Passes)
                {
                    pass.Apply();
                    GraphicsDevice.DrawUserIndexedPrimitives(
                        PrimitiveType.TriangleList,
                        _ringVertices, 0, _ringVertices.Length,
                        pair.Value, 0, pair.Value.Length / 3);
                }
            }
            return;
        }

        float half = RingField.RingPixels / 2f * PlayerPhysics.WorldPerPixel;
        const float z = 390f;   // just behind the player marker

        var corners = new StageVertex[field.Remaining * 4];
        var indices = new int[field.Remaining * 6];
        int quad = 0;

        for (int i = 0; i < field.Count; i++)
        {
            if (field.IsTaken(i)) continue;
            var at = field.WorldPosition(i);
            int v = quad * 4;
            corners[v + 0] = new StageVertex(
                new Vector3(at.X - half, at.Y - half, z), Vector3.Backward, Vector2.Zero);
            corners[v + 1] = new StageVertex(
                new Vector3(at.X + half, at.Y - half, z), Vector3.Backward, Vector2.Zero);
            corners[v + 2] = new StageVertex(
                new Vector3(at.X - half, at.Y + half, z), Vector3.Backward, Vector2.Zero);
            corners[v + 3] = new StageVertex(
                new Vector3(at.X + half, at.Y + half, z), Vector3.Backward, Vector2.Zero);

            int t = quad * 6;
            indices[t + 0] = v; indices[t + 1] = v + 1; indices[t + 2] = v + 2;
            indices[t + 3] = v + 2; indices[t + 4] = v + 1; indices[t + 5] = v + 3;
            quad++;
        }

        _effect.Texture = _ring;
        GraphicsDevice.BlendState = BlendState.NonPremultiplied;
        foreach (var pass in _effect.CurrentTechnique.Passes)
        {
            pass.Apply();
            GraphicsDevice.DrawUserIndexedPrimitives(
                PrimitiveType.TriangleList, corners, 0, quad * 4, indices, 0, quad * 2);
        }
    }

    private void DrawRestartFade()
    {
        float opacity = _engine.RestartFadeOpacity;
        if (opacity <= 0f) return;

        float halfWidth = GraphicsDevice.Viewport.Width / 2f / _zoom;
        float halfHeight = GraphicsDevice.Viewport.Height / 2f / _zoom;
        byte alpha = (byte)Math.Clamp((int)MathF.Round(opacity * byte.MaxValue), 0, byte.MaxValue);
        var vertices = new[]
        {
            new VertexPositionColor(new Vector3(_camera.X - halfWidth, _camera.Y - halfHeight, 1000f),
                                    new Color((byte)0, (byte)0, (byte)0, alpha)),
            new VertexPositionColor(new Vector3(_camera.X + halfWidth, _camera.Y - halfHeight, 1000f),
                                    new Color((byte)0, (byte)0, (byte)0, alpha)),
            new VertexPositionColor(new Vector3(_camera.X - halfWidth, _camera.Y + halfHeight, 1000f),
                                    new Color((byte)0, (byte)0, (byte)0, alpha)),
            new VertexPositionColor(new Vector3(_camera.X + halfWidth, _camera.Y + halfHeight, 1000f),
                                    new Color((byte)0, (byte)0, (byte)0, alpha)),
        };
        var textureEnabled = _effect.TextureEnabled;
        var vertexColorEnabled = _effect.VertexColorEnabled;
        var lightingEnabled = _effect.LightingEnabled;
        float effectAlpha = _effect.Alpha;
        var depthStencil = GraphicsDevice.DepthStencilState;
        var blend = GraphicsDevice.BlendState;
        Color[]? before = null;
        if (RestartSmoke && !_restartSmokeFadeSampled && opacity >= 0.5f)
        {
            _restartSmokeFadeOpacity = opacity;
            try
            {
                before = new Color[GraphicsDevice.PresentationParameters.BackBufferWidth *
                                   GraphicsDevice.PresentationParameters.BackBufferHeight];
                GraphicsDevice.GetBackBufferData(before);
            }
            catch (Exception error)
            {
                _restartSmokeFadeSampled = true;
                _smokeFailure ??= $"restart fade pre-overlay readback failed ({error.GetType().Name})";
            }
        }
        try
        {
            _effect.TextureEnabled = false;
            _effect.VertexColorEnabled = true;
            _effect.LightingEnabled = false;
            _effect.Alpha = 1f;
            GraphicsDevice.DepthStencilState = DepthStencilState.None;
            GraphicsDevice.BlendState = BlendState.AlphaBlend;
            foreach (var pass in _effect.CurrentTechnique.Passes)
            {
                pass.Apply();
                GraphicsDevice.DrawUserIndexedPrimitives(
                    PrimitiveType.TriangleList, vertices, 0, vertices.Length,
                    new[] { 0, 1, 2, 2, 1, 3 }, 0, 2);
                if (RestartSmoke) _restartSmokeFadePositiveOpacityDrawn = true;
            }
            if (before is not null) VerifyRestartSmokeFade(before, alpha, opacity);
        }
        finally
        {
            _effect.TextureEnabled = textureEnabled;
            _effect.VertexColorEnabled = vertexColorEnabled;
            _effect.LightingEnabled = lightingEnabled;
            _effect.Alpha = effectAlpha;
            GraphicsDevice.DepthStencilState = depthStencil;
            GraphicsDevice.BlendState = blend;
        }
    }

    private void VerifyRestartSmokeFade(Color[] before, byte alpha, float opacity)
    {
        _restartSmokeFadeSampled = true;
        _restartSmokeFadeOpacity = opacity;
        var after = new Color[before.Length];
        try
        {
            GraphicsDevice.GetBackBufferData(after);
        }
        catch (Exception error)
        {
            _smokeFailure ??= $"restart fade post-overlay readback failed ({error.GetType().Name})";
            return;
        }

        long sourceRgbSum = 0;
        long resultRgbSum = 0;
        int sourcePixels = 0;
        int mismatchedPixels = 0;
        for (int i = 0; i < before.Length; i++)
        {
            var source = before[i];
            var result = after[i];
            int sourceRgb = source.R + source.G + source.B;
            sourceRgbSum += sourceRgb;
            resultRgbSum += result.R + result.G + result.B;
            if (sourceRgb != 0) sourcePixels++;
            if (!FadeChannelMatches(source.R, result.R, alpha) ||
                !FadeChannelMatches(source.G, result.G, alpha) ||
                !FadeChannelMatches(source.B, result.B, alpha))
                mismatchedPixels++;
        }

        _restartSmokeFadeSourcePixels = sourcePixels;
        _restartSmokeFadeMismatchPixels = mismatchedPixels;
        _restartSmokeFadeSourceRgbSum = sourceRgbSum;
        _restartSmokeFadeResultRgbSum = resultRgbSum;
        _restartSmokeFadePassed = sourcePixels > 0 && resultRgbSum < sourceRgbSum &&
                                  mismatchedPixels == 0;
        if (_restartSmokeFadePassed) return;
        if (sourcePixels == 0)
            _smokeFailure ??= "restart fade verification found no nonblack pre-overlay pixels";
        else if (resultRgbSum >= sourceRgbSum)
            _smokeFailure ??= "restart fade verification did not reduce RGB sum";
        else
            _smokeFailure ??= $"restart fade verification found {mismatchedPixels} mismatched pixels";
    }

    private static bool FadeChannelMatches(byte source, byte result, byte alpha)
    {
        int expected = (source * (byte.MaxValue - alpha) + byte.MaxValue / 2) / byte.MaxValue;
        return Math.Abs(result - expected) <= 2;
    }

    /// <summary>Writes what is currently on screen to a PNG.</summary>
    private void SaveScreenshot(string path)
    {
        int w = GraphicsDevice.PresentationParameters.BackBufferWidth;
        int h = GraphicsDevice.PresentationParameters.BackBufferHeight;
        var data = new Color[w * h];
        GraphicsDevice.GetBackBufferData(data);

        using var texture = new Texture2D(GraphicsDevice, w, h);
        texture.SetData(data);
        using var stream = File.Create(path);
        texture.SaveAsPng(stream, w, h);
        Console.WriteLine($"screenshot {path} ({w}x{h})");
    }

    private void UpdateWindowTitle()
    {
        bool rolling = _engine.Player?.Rolling ?? false;
        int lives = Math.Max(0, _engine.Lives);
        bool gameOver = _engine.GameOver;
        _status = _engine.Status;
        if (_engine.RingCount == _shownRings && rolling == _shownRolling &&
            lives == _shownLives && gameOver == _shownGameOver)
            return;

        _shownRings = _engine.RingCount;
        _shownRolling = rolling;
        _shownLives = lives;
        _shownGameOver = gameOver;
        Window.Title = $"Sonic 4 Episode II - rings {_shownRings}" +
                       (_engine.RingField is null ? "" : $" of {_engine.RingField.Count}") +
                       $" - lives {_shownLives}" +
                       (rolling ? " - rolling" : "") +
                       (gameOver ? " - GAME OVER - R: new run" : " - R: restart");
    }

    private void ResetAttemptPresentation()
    {
        if (_engine.Player is { } player)
            _camera = new Vector2(player.Position.X, player.Position.Y + 40f);
        _followPlayer = true;
        _zoom = 1.6f;
        _playerMotionName = "";
        _playerFrame = 0f;
        _ringsBuiltFor = -1;
        _itemBoxesRemaining = -1;
        _status = _engine.Status;
        _shownRings = -1;
        _shownRolling = !(_engine.Player?.Rolling ?? false);
        _shownLives = int.MinValue;
        _shownGameOver = !_engine.GameOver;
    }

    protected override void Update(GameTime gameTime)
    {
        if (IsSmoke && _smokePendingDraw) return;
        var clock = System.Diagnostics.Stopwatch.StartNew();
        UpdateWindowTitle();

        var keyboard = IsSmoke ? new KeyboardState() : Keyboard.GetState();
        if (keyboard.IsKeyDown(Keys.Escape)) Exit();

        if (keyboard.IsKeyDown(Keys.Tab) && !_tabHeld) _followPlayer = !_followPlayer;
        _tabHeld = keyboard.IsKeyDown(Keys.Tab);
        bool restartPressed = keyboard.IsKeyDown(Keys.R);
        if (restartPressed && !_restartHeld) _engine.RequestRestart();
        _restartHeld = restartPressed;

        // Input is handed to the player before the engine steps, so the player
        // acts on this frame's input rather than last frame's.
        if (IsSmoke)
        {
            bool ready = RestartSmoke && !_restartSmokeRestarted
                ? PrepareRestartSmokeStep()
                : PrepareSmokeStep();
            if (!ready) return;
        }
        else if (_engine.Player is not null)
        {
            if (!_followPlayer)
            {
                _engine.Player.InputX = 0;
                _engine.Player.InputJump = false;
                _engine.Player.InputDown = false;
            }
            else if (_input is not null)
            {
                _input.Apply(_engine.Player,
                             GraphicsDevice.Viewport.Width,
                             GraphicsDevice.Viewport.Height);
            }
            else
            {
                float move = 0f;
                if (keyboard.IsKeyDown(Keys.Left) || keyboard.IsKeyDown(Keys.A)) move -= 1f;
                if (keyboard.IsKeyDown(Keys.Right) || keyboard.IsKeyDown(Keys.D)) move += 1f;
                _engine.Player.InputX = move;
                _engine.Player.InputJump =
                    keyboard.IsKeyDown(Keys.Space) ||
                    keyboard.IsKeyDown(Keys.Z) ||
                    keyboard.IsKeyDown(Keys.Up) ||
                    keyboard.IsKeyDown(Keys.W);
                _engine.Player.InputDown =
                    keyboard.IsKeyDown(Keys.Down) || keyboard.IsKeyDown(Keys.S);
            }
        }

        ulong attempt = _engine.StageAttempt;
        _engine.Step();
        bool attemptChanged = _engine.StageAttempt != attempt;
        if (attemptChanged) ResetAttemptPresentation();
        if (RestartSmoke && !_restartSmokeRestarted) ObserveRestartSmokeStep(attemptChanged);
        else if (IsSmoke) ObserveSmokeStep();
        UpdateWindowTitle();

        float delta = (float)gameTime.ElapsedGameTime.TotalSeconds;

        if (_followPlayer && _engine.Player is not null)
        {
            // Lag the camera behind the player so it eases rather than snapping.
            var target = new Vector2(_engine.Player.Position.X,
                                     _engine.Player.Position.Y + 40f);
            _camera += (target - _camera) * MathHelper.Clamp(delta * 8f, 0f, 1f);
        }
        else
        {
            float pan = 600f * delta / _zoom;
            if (keyboard.IsKeyDown(Keys.Left)) _camera.X -= pan;
            if (keyboard.IsKeyDown(Keys.Right)) _camera.X += pan;
            if (keyboard.IsKeyDown(Keys.Up)) _camera.Y += pan;
            if (keyboard.IsKeyDown(Keys.Down)) _camera.Y -= pan;
        }

        if (keyboard.IsKeyDown(Keys.PageUp)) _zoom *= 1f + delta;
        if (keyboard.IsKeyDown(Keys.PageDown)) _zoom /= 1f + delta;

        clock.Stop();
        _updateTimes.Add(clock.Elapsed.TotalMilliseconds);
        base.Update(gameTime);
    }

    protected override void Draw(GameTime gameTime)
    {
        var clock = System.Diagnostics.Stopwatch.StartNew();
        GraphicsDevice.Clear(NativeScenes is null ? new Color(16, 18, 24) : new Color(68, 102, 255));
        if (_vertices.Length == 0)
        {
            if (IsSmoke)
            {
                _smokeFailure = "empty stage geometry";
                CompleteSmoke();
            }
            else if (ScreenshotPath is not null) Exit();
            return;
        }

        // Posed object geometry is presentation, not simulation, so it belongs
        // here rather than in Update. That is not just tidiness: rebuilding it
        // per simulation step drove Update past the fixed timestep's 16.7 ms
        // budget, and MonoGame answers a slow Update by running more of them to
        // catch up — 28 updates per drawn frame, each redoing work whose result
        // is only ever seen once. Rebuilding per drawn frame breaks that spiral.
        // Screenshot runs drive the animation from the frame counter rather than
        // the clock, so a capture is reproducible. Wall time makes two runs of
        // the same command sample different animation phases, which silently
        // spoils any pixel diff taken between them — that cost me one wrong
        // conclusion about culling before I noticed.
        float animationFrame = IsSmoke ? _smokeUpdates * 0.5f : ScreenshotPath is not null
            ? _frames * 0.5f
            : (float)gameTime.TotalGameTime.TotalSeconds * 30f;

        if (_objectsAnimate)
            BuildObjectBuffers(animationFrame);
        else if (_engine.ItemBoxes is { } boxes && boxes.Remaining != _itemBoxesRemaining)
            BuildObjectBuffers(0f);

        float halfWidth = GraphicsDevice.Viewport.Width / 2f / _zoom;
        float halfHeight = GraphicsDevice.Viewport.Height / 2f / _zoom;

        _effect.World = Matrix.Identity;
        _effect.View = Matrix.CreateLookAt(
            new Vector3(_camera.X, _camera.Y, 2000f),
            new Vector3(_camera.X, _camera.Y, 0f),
            Vector3.Up);
        _effect.Projection = Matrix.CreateOrthographicOffCenter(
            -halfWidth, halfWidth, -halfHeight, halfHeight, 1f, 5000f);

        GraphicsDevice.DepthStencilState = DepthStencilState.Default;
        GraphicsDevice.RasterizerState = RasterizerState.CullNone;
        GraphicsDevice.SamplerStates[0] = SamplerState.LinearWrap;
        // Foliage, railings and window tracery are cut-out textures. Without
        // blending their transparent pixels draw as black silhouettes, which is
        // what the stage looked like before this line.
        GraphicsDevice.BlendState = BlendState.NonPremultiplied;

        // The far background first, deep enough that everything draws over it.
        _phaseMark = System.Diagnostics.Stopwatch.GetTimestamp();
        DrawBackground();
        GraphicsDevice.DepthStencilState = DepthStencilState.Default;
        Phase("sky");

        // One draw per material. The chunking below only applies to the fallback
        // streaming path, which has a per-call primitive limit well below an
        // act's triangle count; resident geometry draws each material in one go.
        const int chunk = 60000 * 3;

        // Our own effect draws the stage; everything else still goes through
        // BasicEffect until it covers those paths too.
        //
        // ON BY DEFAULT as of this beat. It was off while it rendered black, and
        // then while it was believed to be much slower than BasicEffect. Both
        // reasons are gone: it draws correctly, and measured against BasicEffect
        // on the same act it costs 1.50 s/frame against 1.75 — at parity, in fact
        // slightly ahead. The apparent slowness was the vertex transfer that both
        // paths were paying, not the shading.
        //
        // STAGE_FX=off falls back to BasicEffect, =flat is the shading-free
        // diagnostic, =noenv keeps the lighting but suppresses reflections.
        string fxMode = Environment.GetEnvironmentVariable("STAGE_FX") ?? "on";
        // "noenv" is the multi-texture counterpart of "flat": same effect, same
        // lighting, environment maps suppressed. Diffing it against "on" isolates
        // exactly what the reflection contributes and nothing else.
        bool useStageEffect = fxMode is "on" or "flat" or "noenv";
        if (useStageEffect && _stageEffect is not null)
        {
            _stageEffect.Parameters["WorldViewProjection"]?
                .SetValue(_effect.View * _effect.Projection);
            _stageEffect.Parameters["MaterialAmbient"]?
                .SetValue(new Vector3(StageAmbient));
            // NOT RECOVERED — tuned, and flagged as such. Zone 1's tile normals
            // cluster around (0.5, 0.9, -0.1), and the previous invented direction
            // sat nearly perpendicular to them, so N·L collapsed to zero and every
            // surface fell back to ambient alone. This one actually lights the
            // dominant orientation. The engine's own value lives in u_LightSource
            // and is a capture-time constant; see docs/ORACLES.md.
            _stageEffect.Parameters["LightDirection"]?
                .SetValue(Vector3.Normalize(new Vector3(-0.35f, -0.80f, -0.50f)));
            _stageEffect.Parameters["LightDiffuse"]?.SetValue(new Vector3(0.85f));

            // Set explicitly rather than trusting the .fx initialiser to survive
            // compilation. NOT RECOVERED — see Stage.fx; STAGE_ENV overrides it
            // so the contribution can be swept without recompiling the effect.
            float envStrength =
                float.TryParse(Environment.GetEnvironmentVariable("STAGE_ENV"),
                               System.Globalization.NumberStyles.Float,
                               System.Globalization.CultureInfo.InvariantCulture,
                               out float s) ? s : 0.35f;
            _stageEffect.Parameters["EnvironmentStrength"]?.SetValue(envStrength);
        }
        Effect stageEffect = useStageEffect && _stageEffect is not null
            ? _stageEffect : _effect;

        // Bind the resident geometry once for the whole stage; each material is
        // then a span of the shared index buffer.
        bool resident = _vertexBuffer is not null && _indexBuffer is not null &&
                        _batchSpans.Count == _batches.Count;
        if (resident)
        {
            GraphicsDevice.SetVertexBuffer(_vertexBuffer);
            GraphicsDevice.Indices = _indexBuffer;
        }

        // Cull to the columns the camera can actually see. An act is a long
        // strip — Zone F is some 13,000 world units wide — and the follow camera
        // shows around 800 of them, so submitting the whole thing every frame
        // was most of the remaining cost. Padded by one column because a tile is
        // bucketed by its origin and can overhang into the next one.
        // STAGE_CULL=off widens the range to everything, which is the honest way
        // to measure what culling is worth: both configurations then run in the
        // same session against the same contention.
        bool cull = (Environment.GetEnvironmentVariable("STAGE_CULL") ?? "on") != "off";
        int minColumn = cull
            ? StageBatch.Column(_camera.X - halfWidth - StageBatch.ColumnWidth)
            : int.MinValue;
        int maxColumn = cull
            ? StageBatch.Column(_camera.X + halfWidth + StageBatch.ColumnWidth)
            : int.MaxValue;
        int drawnTriangles = 0;

        foreach (var pair in _batches)
        {
            SetBlend(pair.Key);
            var texture = TextureNamed(pair.Key.Base);

            // A material with an environment map needs the technique that reads
            // one. The engine picks a compiled permutation per material the same
            // way; ours has two real ones because we implement base and
            // environment, not the engine's full role set.
            bool wantsEnvironment = pair.Key.Environment is not null && fxMode != "noenv";
            Texture2D? environment = null;

            if (useStageEffect && _stageEffect is not null)
            {
                string wanted = fxMode == "flat" ? "DiagnosticFlat"
                              : wantsEnvironment ? "StageEnvironment"
                              : "StageTechnique";
                if (_stageEffect.CurrentTechnique.Name != wanted)
                    foreach (var t in _stageEffect.Techniques)
                        if (t.Name == wanted) { _stageEffect.CurrentTechnique = t; break; }

                _materialStates.Apply(GraphicsDevice, pair.Key);
                MaterialRenderStates.ConfigureEffect(_stageEffect, pair.Key);
                if (wantsEnvironment) environment = TextureNamed(pair.Key.Environment);
            }
            else
            {
                _effect.Texture = texture;
            }

            foreach (var pass in stageEffect.CurrentTechnique.Passes)
            {
                pass.Apply();
                // The textures go on AFTER Apply. MonoGame writes the effect's
                // own sampler state during Apply, so a texture set before it is
                // discarded — which is what left every texel reading black.
                if (useStageEffect && _stageEffect is not null)
                {
                    GraphicsDevice.Textures[0] = texture;
                    GraphicsDevice.SamplerStates[0] = SamplerState.LinearWrap;
                    if (environment is not null)
                    {
                        GraphicsDevice.Textures[1] = environment;
                        // Wrapped, and it has to be: the recovered paraboloid
                        // projection puts one hemisphere at u in [-0.5, 0], which
                        // only lands on the far half of the atlas by wrapping.
                        GraphicsDevice.SamplerStates[1] = SamplerState.LinearWrap;
                    }
                }
                if (resident)
                {
                    var visible = VisibleSpan(_batchSpans[pair.Key], minColumn, maxColumn);
                    if (visible is { } span)
                    {
                        GraphicsDevice.DrawIndexedPrimitives(
                            PrimitiveType.TriangleList, 0, span.Start, span.Count / 3);
                        drawnTriangles += span.Count / 3;
                    }
                }
                else
                {
                    var indices = pair.Value;
                    for (int start = 0; start < indices.Length; start += chunk)
                    {
                        int count = Math.Min(chunk, indices.Length - start);
                        GraphicsDevice.DrawUserIndexedPrimitives(
                            PrimitiveType.TriangleList,
                            _vertices, 0, _vertices.Length,
                            indices, start, count / 3);
                    }
                }
            }
        }
        if (resident && !_reportedCulling)
        {
            _reportedCulling = true;
            int all = _batchSpans.Values.Sum(s => s.Sum(x => x.Count / 3));
            Console.WriteLine(
                $"culling: {drawnTriangles:N0} of {all:N0} triangles drawn " +
                $"({(all > 0 ? drawnTriangles * 100.0 / all : 0):F1}%), " +
                $"columns {minColumn}..{maxColumn}");
        }

        // Objects, rings and the player still stream their geometry — they are
        // small and rebuilt as they animate, so the upload is not the cost there.
        // Release the stage's buffers so those paths bind their own.
        if (resident)
        {
            GraphicsDevice.SetVertexBuffer(null);
            GraphicsDevice.Indices = null;
        }

        Phase("stage");
        GraphicsDevice.DepthStencilState = DepthStencilState.Default;
        DrawObjects(cull ? _camera.X - halfWidth : float.NegativeInfinity,
                    cull ? _camera.X + halfWidth : float.PositiveInfinity);
        Phase("objects");
        DrawRings();
        Phase("rings");
        DrawPlayer();
        Phase("player");
        DrawRestartFade();
        base.Draw(gameTime);
        Phase("present");

        clock.Stop();
        _frameTimes.Add(clock.Elapsed.TotalMilliseconds);

        if (IsSmoke)
        {
            _smokeDraws++;
            if (_smokePendingDraw) CompleteSmoke();
        }
        else if (ScreenshotPath is not null && ++_frames >= ScreenshotFrame)
        {
            ReportFrameTimes();
            SaveScreenshot(ScreenshotPath);
            Exit();
        }
    }

    private bool PrepareSmokeRequirements()
    {
        bool motions = new[] { "SON_FWWAIT0_01", "SON_WALK", "SON_RUN", "SON_SPIN01" }
            .All(name => _playerMotions.TryGetValue(name, out var motion) && motion.Channels.Count > 0 &&
                         float.IsFinite(motion.Start) && float.IsFinite(motion.End) && motion.End > motion.Start);
        if (_engine.Player is null || _engine.Collision?.HasShapes != true ||
            _vertices.Length == 0 || !_batches.Values.Any(indices => indices.Length >= 3) ||
            _playerModel is null || _playerBallModel is null || !motions)
        {
            _smokeFailure ??= "missing stage, collision, player model or motion";
            _smokePendingDraw = true;
            return false;
        }
        return true;
    }

    private bool PrepareSmokeStep()
    {
        if (!PrepareSmokeRequirements()) return false;

        var player = _engine.Player!;
        if (_smokeUpdates == 0) _smokeStart = player.Position;
        player.InputX = _smokeUpdates is >= 30 and < 150 ? 1 : 0;
        player.InputDown = false;
        player.InputJump = !_smokeJumpIssued && _smokeUpdates >= 60 && player.OnGround;
        _smokeJumpIssued |= player.InputJump;
        return true;
    }

    private bool PrepareRestartSmokeStep()
    {
        if (!PrepareSmokeRequirements()) return false;
        if (!_restartSmokeCaptured) CaptureRestartSmoke();

        if (!_restartSmokeDamageApplied)
        {
            if (_restartSmokeInitialRings != 0)
            {
                _smokeFailure ??= "restart smoke requires zero initial rings";
                _smokePendingDraw = true;
                return false;
            }

            _restartSmokeDamageApplied = true;
            var damage = _engine.DamagePlayer();
            if (damage.Outcome != DamageOutcome.Death || _engine.Player?.IsDead != true)
            {
                _smokeFailure ??= "damage did not enter death";
                _smokePendingDraw = true;
                return false;
            }
        }
        return true;
    }

    private void CaptureRestartSmoke()
    {
        _restartSmokeCaptured = true;
        _restartSmokeInitialAttempt = _engine.StageAttempt;
        _restartSmokeOldPlayer = _engine.Player;
        _restartSmokeOldStage = _engine.Stage;
        _restartSmokeOldCollision = _engine.Collision;
        _restartSmokeOldTasks = _engine.Scheduler.Tasks.ToArray();
        _restartSmokeInitialLives = _engine.Lives;
        _restartSmokeObservedLives = _engine.Lives;
        _restartSmokeInitialRings = _engine.RingCount;
        _restartSmokeInitialRingFieldCount = _engine.RingField?.Count ?? -1;
        _restartSmokeInitialItemBoxes = _engine.ItemBoxes?.Remaining ?? -1;
        _restartSmokeInitialTaskCount = _engine.Scheduler.Count;
        _restartSmokeInitialObjectCount = _engine.Objects.Count;
    }

    private void ObserveRestartSmokeStep(bool attemptChanged)
    {
        _restartSmokeTotalUpdates++;
        _restartSmokeDeathUpdates++;
        var player = _engine.Player;
        if (player is null)
            _smokeFailure ??= "missing player during restart";
        else
        {
            var p = player.Position;
            var v = player.Velocity;
            if (!float.IsFinite(p.X) || !float.IsFinite(p.Y) || !float.IsFinite(p.Z) ||
                !float.IsFinite(v.X) || !float.IsFinite(v.Y))
                _smokeFailure ??= "nonfinite player during restart";
        }
        if (!float.IsFinite(_engine.DeathWaitTime) || !float.IsFinite(_engine.RestartFadeOpacity))
            _smokeFailure ??= "nonfinite restart timer";
        _restartSmokeSawDead |= _restartSmokeOldPlayer?.IsDead == true;
        if (_engine.Lives != _restartSmokeObservedLives)
        {
            _restartSmokeLifeChanges++;
            _restartSmokeObservedLives = _engine.Lives;
        }

        if (attemptChanged)
        {
            VerifyRestartSmoke();
            if (_smokeFailure is null) ResetRestartMovementSmoke();
            else _smokePendingDraw = true;
            return;
        }

        if (_smokeFailure is not null || _restartSmokeDeathUpdates >= SmokeUpdates)
        {
            _smokeFailure ??= "restart did not occur within 180 updates";
            _smokePendingDraw = true;
        }
    }

    private void VerifyRestartSmoke()
    {
        _restartSmokeLivesAfterRestart = _engine.Lives;
        _restartSmokeTaskCountAfterRestart = _engine.Scheduler.Count;
        _restartSmokeObjectCountAfterRestart = _engine.Objects.Count;
        _restartSmokeOldPlayerDestroyed = _restartSmokeOldPlayer?.Destroyed == true;
        _restartSmokeOldTasksDeleted = _restartSmokeOldTasks.All(task => task.Deleted);
        _restartSmokeStageRetained = ReferenceEquals(_restartSmokeOldStage, _engine.Stage);
        _restartSmokeCollisionRetained = ReferenceEquals(_restartSmokeOldCollision, _engine.Collision);
        _restartSmokeNewPlayerAlive = _engine.Player is { } player &&
                                      !ReferenceEquals(_restartSmokeOldPlayer, player) &&
                                      !player.IsDead && !player.Destroyed;
        _restartSmokeRingsReset = _engine.RingCount == 0 &&
                                  _engine.RingField is { Collected: 0 } rings &&
                                  rings.Count == _restartSmokeInitialRingFieldCount &&
                                  _engine.ItemBoxes?.Remaining == _restartSmokeInitialItemBoxes;
        _restartSmokeStateReset = !_engine.GameOver && _engine.DeathWaitTime == 0f &&
                                  _engine.RestartFadeOpacity == 0f && !_engine.ActClear &&
                                  _engine.StageFrame == 1;
        _restartSmokeTaskCardinalityReset = _restartSmokeTaskCountAfterRestart == _restartSmokeInitialTaskCount &&
                                            _restartSmokeObjectCountAfterRestart == _restartSmokeInitialObjectCount;
        _restartSmokeLivesConsumedOnce = _restartSmokeLivesAfterRestart == _restartSmokeInitialLives - 1 &&
                                         _restartSmokeLifeChanges == 1;
        _restartSmokeRestarted = _engine.StageAttempt == _restartSmokeInitialAttempt + 1 &&
                                 _restartSmokeDamageApplied && _restartSmokeSawDead &&
                                 _restartSmokeOldPlayerDestroyed && _restartSmokeOldTasksDeleted &&
                                 _restartSmokeStageRetained && _restartSmokeCollisionRetained &&
                                 _restartSmokeNewPlayerAlive && _restartSmokeRingsReset &&
                                 _restartSmokeStateReset && _restartSmokeTaskCardinalityReset &&
                                 _restartSmokeLivesConsumedOnce;
        if (!_restartSmokeRestarted)
            _smokeFailure ??= "restart invariants failed";
    }

    private void ResetRestartMovementSmoke()
    {
        _smokeUpdates = 0;
        _smokeDraws = 0;
        _smokePlayerDrawn = false;
        _smokeStart = default;
        _smokeJumpIssued = false;
        _smokeRose = false;
        _smokeLanded = false;
        _smokePendingDraw = false;
    }

    private void ObserveSmokeStep()
    {
        if (RestartSmoke) _restartSmokeTotalUpdates++;
        var player = _engine.Player;
        if (player is null)
        {
            _smokeFailure ??= "missing player";
            _smokePendingDraw = true;
            return;
        }

        _smokeUpdates++;
        var p = player.Position;
        var v = player.Velocity;
        if (!float.IsFinite(p.X) || !float.IsFinite(p.Y) || !float.IsFinite(p.Z) ||
            !float.IsFinite(v.X) || !float.IsFinite(v.Y) || player.IsDead)
            _smokeFailure ??= "nonfinite or dead player";
        _smokeRose |= _smokeJumpIssued && !player.OnGround && v.Y > 0;
        _smokeLanded |= _smokeRose && player.OnGround;
        if (_smokeUpdates >= SmokeUpdates || _smokeFailure is not null)
        {
            if (_smokeFailure is null &&
                (p.X - _smokeStart.X <= PlayerPhysics.WorldPerPixel || !_smokeRose || !_smokeLanded))
                _smokeFailure = "movement, jump or landing was not observed";
            player.InputX = 0;
            player.InputJump = false;
            player.InputDown = false;
            _smokePendingDraw = true;
        }
    }

    private void CompleteSmoke()
    {
        var end = _engine.Player?.Position ?? _smokeStart;
        if (!_smokePlayerDrawn) _smokeFailure ??= "player geometry was not drawn";
        bool fadePassed = !RestartSmoke || CompleteRestartSmokeFadeVerification();
        bool movementPassed = _smokeFailure is null && _smokeUpdates == SmokeUpdates && _smokeDraws > 0;
        if (ScreenshotPath is not null) SaveScreenshot(ScreenshotPath);
        var options = new System.Text.Json.JsonSerializerOptions
        {
            NumberHandling = System.Text.Json.Serialization.JsonNumberHandling.AllowNamedFloatingPointLiterals
        };
        if (RestartSmoke)
        {
            bool restartPassed = _restartSmokeRestarted && _restartSmokeDamageApplied &&
                                 _restartSmokeSawDead && _restartSmokeOldPlayerDestroyed &&
                                 _restartSmokeOldTasksDeleted && _restartSmokeStageRetained &&
                                 _restartSmokeCollisionRetained && _restartSmokeNewPlayerAlive &&
                                 _restartSmokeRingsReset && _restartSmokeStateReset &&
                                 _restartSmokeTaskCardinalityReset && _restartSmokeLivesConsumedOnce &&
                                 fadePassed;
            bool passed = movementPassed && restartPassed;
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
            {
                type = "restart_smoke", passed, updates = _smokeUpdates, renderedFrames = _smokeDraws,
                startX = _smokeStart.X, startY = _smokeStart.Y, endX = end.X, endY = end.Y,
                deltaX = end.X - _smokeStart.X, jumpIssued = _smokeJumpIssued,
                rose = _smokeRose, landed = _smokeLanded, playerDrawn = _smokePlayerDrawn,
                totalUpdates = _restartSmokeTotalUpdates, deathUpdates = _restartSmokeDeathUpdates,
                attemptStart = _restartSmokeInitialAttempt, attemptEnd = _engine.StageAttempt,
                damageApplied = _restartSmokeDamageApplied, sawDead = _restartSmokeSawDead,
                oldPlayerDestroyed = _restartSmokeOldPlayerDestroyed,
                oldTasksDeleted = _restartSmokeOldTasksDeleted,
                stageRetained = _restartSmokeStageRetained,
                collisionRetained = _restartSmokeCollisionRetained,
                initialLives = _restartSmokeInitialLives,
                livesAfterRestart = _restartSmokeLivesAfterRestart,
                lifeChanges = _restartSmokeLifeChanges,
                livesConsumedOnce = _restartSmokeLivesConsumedOnce,
                initialRings = _restartSmokeInitialRings,
                ringsReset = _restartSmokeRingsReset,
                stateReset = _restartSmokeStateReset,
                tasksBeforeRestart = _restartSmokeInitialTaskCount,
                tasksAfterRestart = _restartSmokeTaskCountAfterRestart,
                objectsBeforeRestart = _restartSmokeInitialObjectCount,
                objectsAfterRestart = _restartSmokeObjectCountAfterRestart,
                taskCardinalityReset = _restartSmokeTaskCardinalityReset,
                fadePositiveOpacityDrawn = _restartSmokeFadePositiveOpacityDrawn,
                fadeSampled = _restartSmokeFadeSampled,
                fadePassed = _restartSmokeFadePassed,
                fadeSampleOpacity = _restartSmokeFadeOpacity,
                fadeSourcePixels = _restartSmokeFadeSourcePixels,
                fadeMismatchedPixels = _restartSmokeFadeMismatchPixels,
                fadeSourceRgbSum = _restartSmokeFadeSourceRgbSum,
                fadeResultRgbSum = _restartSmokeFadeResultRgbSum,
                newPlayerAlive = _restartSmokeNewPlayerAlive, failure = _smokeFailure
            }, options));
            PlaybackSmokeExitCode = passed ? 0 : 1;
        }
        else
        {
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
            {
                type = "playback_smoke", passed = movementPassed, updates = _smokeUpdates,
                renderedFrames = _smokeDraws, startX = _smokeStart.X, startY = _smokeStart.Y,
                endX = end.X, endY = end.Y, deltaX = end.X - _smokeStart.X,
                jumpIssued = _smokeJumpIssued, rose = _smokeRose, landed = _smokeLanded,
                playerDrawn = _smokePlayerDrawn, failure = _smokeFailure
            }, options));
            PlaybackSmokeExitCode = movementPassed ? 0 : 1;
        }
        Exit();
    }

    private bool CompleteRestartSmokeFadeVerification()
    {
        if (!_restartSmokeFadePositiveOpacityDrawn)
        {
            _smokeFailure ??= "restart fade never submitted a positive-opacity overlay";
            return false;
        }
        if (!_restartSmokeFadeSampled)
        {
            _smokeFailure ??= "restart fade verification never sampled opacity >= 0.5";
            return false;
        }
        if (!_restartSmokeFadePassed)
        {
            _smokeFailure ??= "restart fade verification failed";
            return false;
        }
        return true;
    }

    /// <summary>
    /// Reports the draw cost, leading with the fastest frame.
    /// </summary>
    /// <remarks>
    /// The first few frames include shader compilation and driver warm-up, so
    /// they are dropped. The minimum is the headline because contention can only
    /// push a sample up.
    /// </remarks>
    protected override void Dispose(bool disposing)
    {
        if (disposing) _materialStates.Dispose();
        base.Dispose(disposing);
    }

    private void ReportFrameTimes()
    {
        var samples = _frameTimes.Skip(Math.Min(3, _frameTimes.Count - 1))
                                 .OrderBy(t => t).ToList();
        if (samples.Count == 0) return;
        Console.WriteLine(
            $"draw:   min {samples[0]:F1} ms, median {samples[samples.Count / 2]:F1} ms, " +
            $"max {samples[^1]:F1} ms over {samples.Count} frames");

        int drawn = Math.Max(1, _frames);
        foreach (var (name, total) in _phaseTimes.OrderByDescending(kv => kv.Value))
            Console.WriteLine($"  {name,-8} {total / drawn,6:F2} ms/frame");

        var updates = _updateTimes.Skip(Math.Min(3, _updateTimes.Count - 1))
                                  .OrderBy(t => t).ToList();
        if (updates.Count > 0)
            Console.WriteLine(
                $"update: min {updates[0]:F1} ms, median {updates[updates.Count / 2]:F1} ms, " +
                $"max {updates[^1]:F1} ms over {updates.Count} frames");
    }
}
