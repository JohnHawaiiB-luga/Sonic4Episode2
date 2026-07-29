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
    private VertexPositionNormalTexture[] _vertices = [];
    private readonly Dictionary<string, int[]> _batches = [];
    private readonly Dictionary<string, Texture2D> _textures = [];
    private StageBatch? _pending;
    private Texture2D _white = null!;
    private Texture2D _marker = null!;
    private Texture2D _ring = null!;
    private TileMesh? _ringMesh;
    private VertexPositionNormalTexture[] _objectVertices = [];
    private readonly Dictionary<string, int[]> _objectBatches = [];
    private int _objectInstances;
    private VertexPositionNormalTexture[] _skyVertices = [];
    private readonly Dictionary<string, int[]> _skyBatches = [];
    private float _skyCenterX, _skyCenterY;
    private VertexPositionNormalTexture[] _ringVertices = [];
    private readonly Dictionary<string, int[]> _ringBatches = [];
    private int _ringsBuiltFor = -1;
    private GameEngine _engine = null!;

    private Vector2 _camera;
    private float _zoom = 1f;
    private bool _followPlayer;
    private bool _tabHeld;
    private string _status = "";
    private int _shownRings = -1;
    private bool _shownRolling;

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

    /// <summary>Frames to run before the screenshot is taken.</summary>
    public int ScreenshotFrame { get; set; } =
        int.TryParse(Environment.GetEnvironmentVariable("SCREENSHOT_FRAME"), out int f) ? f : 30;

    /// <summary>Cell to drop the player into; see <see cref="GameEngine.SpawnCellX"/>.</summary>
    public int? SpawnCellX { get; set; }

    /// <inheritdoc cref="GameEngine.SpawnCellY"/>
    public int? SpawnCellY { get; set; }

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
        _vertices = new VertexPositionNormalTexture[batch.VertexCount];
        for (int i = 0; i < batch.VertexCount; i++)
        {
            _vertices[i] = new VertexPositionNormalTexture(
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
                // The V axis points the other way in a texture than in the
                // model data, same flip the OBJ exporter needs.
                new Vector2(batch.TexCoords[i * 2], 1f - batch.TexCoords[i * 2 + 1]));
        }
        foreach (var pair in batch.IndicesByTexture)
            _batches[pair.Key] = [.. pair.Value];
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
            VertexColorEnabled = false,
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
        // Our own compiled effect, loaded beside the executable. Missing or
        // broken, the stage falls back to BasicEffect rather than failing.
        try
        {
            string fx = Path.Combine(AppContext.BaseDirectory, "Content", "Stage.mgfx");
            if (File.Exists(fx))
                _stageEffect = new Effect(GraphicsDevice, File.ReadAllBytes(fx));
        }
        catch (Exception ex)
        {
            Console.WriteLine($"stage effect not loaded ({ex.GetType().Name}); " +
                              "falling back to BasicEffect");
            _stageEffect = null;
        }
        Console.WriteLine(_stageEffect is null
            ? "stage effect: BasicEffect (fallback)"
            : "stage effect: Stage.mgfx (recovered material model)");

        _white = new Texture2D(GraphicsDevice, 1, 1);
        _white.SetData(new[] { Color.Gray });
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
        if (_engine.Player is null || !_followPlayer) return;

        float x = _engine.Player.Position.X;
        float y = _engine.Player.Position.Y;
        float halfWidth = Player.Width / 2f;
        float height = Player.Height;
        const float z = 400f;   // in front of every stage layer

        var corners = new[]
        {
            new VertexPositionNormalTexture(new Vector3(x - halfWidth, y, z), Vector3.Backward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(x + halfWidth, y, z), Vector3.Backward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(x - halfWidth, y + height, z), Vector3.Backward, Vector2.Zero),
            new VertexPositionNormalTexture(new Vector3(x + halfWidth, y + height, z), Vector3.Backward, Vector2.Zero),
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
        if (_engine.Player is null || !_followPlayer) return;
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
        float frame = motion.Start + ((_playerFrame - motion.Start) % span);

        var world = AnimatedPose.World(model.Nodes, motion.Channels, frame);
        var mesh = TileMesh.Skinned(model, world);

        var player = _engine.Player;
        float yaw = player.FacingLeft ? -MathF.Tau / 4f : MathF.Tau / 4f;
        var pose = System.Numerics.Matrix4x4.CreateRotationY(yaw) *
                   System.Numerics.Matrix4x4.CreateTranslation(
                       player.Position.X, player.Position.Y, 400f);

        var vertices = new VertexPositionNormalTexture[mesh.Positions.Length / 3];
        for (int i = 0; i < vertices.Length; i++)
        {
            var p = System.Numerics.Vector3.Transform(new System.Numerics.Vector3(
                mesh.Positions[i * 3], mesh.Positions[i * 3 + 1],
                mesh.Positions[i * 3 + 2]), pose);
            vertices[i] = new VertexPositionNormalTexture(
                new Vector3(p.X, p.Y, p.Z), Vector3.Backward,
                new Vector2(mesh.TexCoords[i * 2], 1f - mesh.TexCoords[i * 2 + 1]));
        }

        // Group triangles by texture, the same shape StageBatch produces.
        var groups = new Dictionary<string, List<int>>();
        for (int t = 0; t < mesh.TriangleTextures.Length; t++)
        {
            string key = mesh.TriangleTextures[t] ?? "";
            if (mesh.TriangleBlends[t] == MaterialBlend.Additive)
                key = StageBatch.AdditivePrefix + key;
            if (!groups.TryGetValue(key, out var list)) groups[key] = list = [];
            list.Add(mesh.Indices[t * 3]);
            list.Add(mesh.Indices[t * 3 + 1]);
            list.Add(mesh.Indices[t * 3 + 2]);
        }

        foreach (var pair in groups)
        {
            SetBlend(pair.Key);
            _effect.Texture = _textures.TryGetValue(
                StageBatch.TextureOf(pair.Key).ToUpperInvariant(), out var t)
                ? t : _white;
            var indices = pair.Value;
            foreach (var pass in _effect.CurrentTechnique.Passes)
            {
                pass.Apply();
                GraphicsDevice.DrawUserIndexedPrimitives(
                    PrimitiveType.TriangleList, vertices, 0, vertices.Length,
                    [.. indices], 0, indices.Count / 3);
            }
        }
        GraphicsDevice.BlendState = BlendState.AlphaBlend;
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
            _objectPlacements.Add((obj, placement.X * scale, -placement.Y * scale));
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
        // A broken item box stops being drawn. Positions match exactly: the
        // viewer and ItemBoxes derive world coordinates the same way.
        var broken = new HashSet<(float, float)>();
        var boxes = _engine.ItemBoxes;
        if (boxes is not null)
            for (int i = 0; i < boxes.Count; i++)
                if (boxes.IsBroken(i))
                {
                    var p = boxes.PositionOf(i);
                    broken.Add((p.X, p.Y));
                }
        _itemBoxesRemaining = boxes?.Remaining ?? -1;

        var batch = new StageBatch();
        foreach (var (obj, x, y) in _objectPlacements)
        {
            if (broken.Contains((x, y))) continue;
            TileMesh mesh = obj.Rest!;
            if (obj.Model is not null && obj.Channels.Count > 0)
            {
                float f = obj.Start + (frame % MathF.Max(obj.End - obj.Start, 1f));
                var world = AnimatedPose.World(obj.Model.Nodes, obj.Channels, f);
                mesh = TileMesh.Posed(obj.Model, world);
            }
            batch.Add(mesh, x, y, 385f);
        }

        _objectVertices = new VertexPositionNormalTexture[batch.VertexCount];
        for (int i = 0; i < _objectVertices.Length; i++)
        {
            _objectVertices[i] = new VertexPositionNormalTexture(
                new Vector3(batch.Positions[i * 3],
                            batch.Positions[i * 3 + 1],
                            batch.Positions[i * 3 + 2]),
                Vector3.Backward,
                new Vector2(batch.TexCoords[i * 2], 1f - batch.TexCoords[i * 2 + 1]));
        }
        _objectBatches.Clear();
        foreach (var pair in batch.IndicesByTexture)
            _objectBatches[pair.Key] = [.. pair.Value];
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
            _skyCenterX = 0f;
            _skyCenterY = _engine.Stage!.MaxY - (batch.MaxY - batch.MinY) * 0.25f;

            _skyVertices = new VertexPositionNormalTexture[batch.VertexCount];
            for (int i = 0; i < _skyVertices.Length; i++)
                _skyVertices[i] = new VertexPositionNormalTexture(
                    new Vector3(batch.Positions[i * 3], batch.Positions[i * 3 + 1],
                                batch.Positions[i * 3 + 2]),
                    Vector3.Backward,
                    new Vector2(batch.TexCoords[i * 2], 1f - batch.TexCoords[i * 2 + 1]));
            foreach (var pair in batch.IndicesByTexture)
                _skyBatches[pair.Key] = [.. pair.Value];

            Console.WriteLine($"background loaded: {_skyVertices.Length:N0} vertices");
        }
        catch (Exception ex) when (ex is AmbException or NnException) { }
    }

    /// <summary>
    /// Draws the far background, parallaxed against the camera.
    /// </summary>
    /// <remarks>
    /// The background sits at a fraction of the camera's motion, the shorthand
    /// every side-scroller uses for distance, and is pinned near the top of the
    /// stage where the sky belongs.
    /// </remarks>
    private void DrawBackground()
    {
        if (_skyVertices.Length == 0) return;

        // Camera-locked with parallax: the background follows the camera but drifts
        // slower, so it reads as far away. The horizontal lock keeps it filling the
        // view; the vertical anchor keeps the sky above the level.
        float px = _camera.X * 0.85f;
        float py = _skyCenterY + _camera.Y * 0.15f;
        _effect.World = Matrix.CreateTranslation(px, py, 0f);

        foreach (var pair in _skyBatches)
        {
            SetBlend(pair.Key);
            _effect.Texture = _textures.TryGetValue(
                StageBatch.TextureOf(pair.Key).ToUpperInvariant(), out var t)
                ? t : _white;
            foreach (var pass in _effect.CurrentTechnique.Passes)
            {
                pass.Apply();
                GraphicsDevice.DrawUserIndexedPrimitives(
                    PrimitiveType.TriangleList, _skyVertices, 0, _skyVertices.Length,
                    pair.Value, 0, pair.Value.Length / 3);
            }
        }
        _effect.World = Matrix.Identity;
    }

    /// <summary>
    /// Sets the blend state for a batch key — additive for glow materials,
    /// ordinary transparency otherwise.
    /// </summary>
    /// <remarks>
    /// Additive is <c>SRCALPHA / ONE</c>, which is what the material's own render
    /// state asks for. It is not MonoGame's <c>BlendState.Additive</c>, which is
    /// <c>ONE / ONE</c> and blows out anything not pre-multiplied.
    /// </remarks>
    private static readonly BlendState AdditiveSrcAlpha = new()
    {
        ColorSourceBlend = Blend.SourceAlpha,
        ColorDestinationBlend = Blend.One,
        AlphaSourceBlend = Blend.SourceAlpha,
        AlphaDestinationBlend = Blend.One,
    };

    private void SetBlend(string key) =>
        GraphicsDevice.BlendState = StageBatch.IsAdditive(key)
            ? AdditiveSrcAlpha : BlendState.AlphaBlend;

    /// <summary>Draws the placed object models.</summary>
    private void DrawObjects()
    {
        if (_objectVertices.Length == 0) return;
        foreach (var pair in _objectBatches)
        {
            SetBlend(pair.Key);
            _effect.Texture = _textures.TryGetValue(
                StageBatch.TextureOf(pair.Key).ToUpperInvariant(), out var t)
                ? t : _white;
            foreach (var pass in _effect.CurrentTechnique.Passes)
            {
                pass.Apply();
                GraphicsDevice.DrawUserIndexedPrimitives(
                    PrimitiveType.TriangleList,
                    _objectVertices, 0, _objectVertices.Length,
                    pair.Value, 0, pair.Value.Length / 3);
            }
        }
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

        _ringVertices = new VertexPositionNormalTexture[batch.VertexCount];
        for (int i = 0; i < _ringVertices.Length; i++)
        {
            _ringVertices[i] = new VertexPositionNormalTexture(
                new Vector3(batch.Positions[i * 3],
                            batch.Positions[i * 3 + 1],
                            batch.Positions[i * 3 + 2]),
                Vector3.Backward,
                new Vector2(batch.TexCoords[i * 2], batch.TexCoords[i * 2 + 1]));
        }
        foreach (var pair in batch.IndicesByTexture)
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
                _effect.Texture = _textures.TryGetValue(pair.Key.ToUpperInvariant(),
                                                        out var texture)
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

        var corners = new VertexPositionNormalTexture[field.Remaining * 4];
        var indices = new int[field.Remaining * 6];
        int quad = 0;

        for (int i = 0; i < field.Count; i++)
        {
            if (field.IsTaken(i)) continue;
            var at = field.WorldPosition(i);
            int v = quad * 4;
            corners[v + 0] = new VertexPositionNormalTexture(
                new Vector3(at.X - half, at.Y - half, z), Vector3.Backward, Vector2.Zero);
            corners[v + 1] = new VertexPositionNormalTexture(
                new Vector3(at.X + half, at.Y - half, z), Vector3.Backward, Vector2.Zero);
            corners[v + 2] = new VertexPositionNormalTexture(
                new Vector3(at.X - half, at.Y + half, z), Vector3.Backward, Vector2.Zero);
            corners[v + 3] = new VertexPositionNormalTexture(
                new Vector3(at.X + half, at.Y + half, z), Vector3.Backward, Vector2.Zero);

            int t = quad * 6;
            indices[t + 0] = v; indices[t + 1] = v + 1; indices[t + 2] = v + 2;
            indices[t + 3] = v + 2; indices[t + 4] = v + 1; indices[t + 5] = v + 3;
            quad++;
        }

        _effect.Texture = _ring;
        foreach (var pass in _effect.CurrentTechnique.Passes)
        {
            pass.Apply();
            GraphicsDevice.DrawUserIndexedPrimitives(
                PrimitiveType.TriangleList, corners, 0, quad * 4, indices, 0, quad * 2);
        }
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

    protected override void Update(GameTime gameTime)
    {
        bool rolling = _engine.Player?.Rolling ?? false;
        if (_engine.RingCount != _shownRings || rolling != _shownRolling)
        {
            _shownRings = _engine.RingCount;
            _shownRolling = rolling;
            Window.Title = $"Sonic 4 Episode II - rings {_shownRings}" +
                           (_engine.RingField is null
                               ? "" : $" of {_engine.RingField.Count}") +
                           (rolling ? " - rolling" : "");
        }

        // Play the object animations by rebuilding their posed geometry each
        // frame. Only when something actually animates, so a static act pays
        // nothing. A box breaking also forces one rebuild so it stops drawing,
        // even on a stage where nothing else moves.
        if (_objectsAnimate)
            BuildObjectBuffers((float)gameTime.TotalGameTime.TotalSeconds * 30f);
        else if (_engine.ItemBoxes is { } boxes && boxes.Remaining != _itemBoxesRemaining)
            BuildObjectBuffers(0f);

        var keyboard = Keyboard.GetState();
        if (keyboard.IsKeyDown(Keys.Escape)) Exit();

        if (keyboard.IsKeyDown(Keys.Tab) && !_tabHeld) _followPlayer = !_followPlayer;
        _tabHeld = keyboard.IsKeyDown(Keys.Tab);

        // Input is handed to the player before the engine steps, so the player
        // acts on this frame's input rather than last frame's.
        if (_engine.Player is not null && _followPlayer)
        {
            if (_input is not null)
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

        _engine.Step();

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

        base.Update(gameTime);
    }

    protected override void Draw(GameTime gameTime)
    {
        GraphicsDevice.Clear(new Color(16, 18, 24));
        if (_vertices.Length == 0) return;

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
        GraphicsDevice.BlendState = BlendState.AlphaBlend;

        // The far background first, deep enough that everything draws over it.
        DrawBackground();

        // One draw per texture. DrawUserIndexedPrimitives also has a per-call
        // primitive limit well below an act's triangle count, so each batch is
        // chunked as well.
        const int chunk = 60000 * 3;

        // Our own effect draws the stage when it loaded; everything else still
        // goes through BasicEffect until it covers those paths too.
        //
        // OFF BY DEFAULT — it compiles, loads and binds without error but renders
        // the stage black, so it is not yet correct and must not be the default.
        // Two causes ruled out: the SV_POSITION semantic (wrong for vs_3_0, now
        // POSITION0) and matrix packing (row_major is rejected by MGFX's
        // parameter writer). Next suspects are the WorldViewProjection transpose
        // convention MonoGame applies on SetValue, and whether the sampler is
        // actually bound through sampler_state under MojoShader. Set to true to
        // resume debugging.
        // STAGE_FX=on uses the real technique, STAGE_FX=flat the diagnostic that
        // ignores shading entirely. Unset means BasicEffect, which is the known
        // working path while the effect is still wrong.
        string fxMode = Environment.GetEnvironmentVariable("STAGE_FX") ?? "off";
        bool useStageEffect = fxMode is "on" or "flat";
        if (useStageEffect && _stageEffect is not null)
        {
            var wanted = fxMode == "flat" ? "DiagnosticFlat" : "StageTechnique";
            foreach (var t in _stageEffect.Techniques)
                if (t.Name == wanted) { _stageEffect.CurrentTechnique = t; break; }
        }
        if (useStageEffect && _stageEffect is not null)
        {
            _stageEffect.Parameters["WorldViewProjection"]?
                .SetValue(_effect.View * _effect.Projection);
            _stageEffect.Parameters["MaterialAmbient"]?
                .SetValue(new Vector3(StageAmbient));
            _stageEffect.Parameters["LightDirection"]?
                .SetValue(Vector3.Normalize(new Vector3(0.3f, 0.6f, 0.75f)));
            _stageEffect.Parameters["LightDiffuse"]?.SetValue(new Vector3(0.85f));
        }
        Effect stageEffect = useStageEffect && _stageEffect is not null
            ? _stageEffect : _effect;

        foreach (var pair in _batches)
        {
            SetBlend(pair.Key);
            var texture = _textures.TryGetValue(
                StageBatch.TextureOf(pair.Key).ToUpperInvariant(), out var t)
                ? t
                : _white;
            if (useStageEffect && _stageEffect is not null)
            {
                // Straight onto the device sampler slot; the effect declares
                // its sampler at register s0 rather than via sampler_state.
                GraphicsDevice.Textures[0] = texture;
                // Diffuse is white until the batch key carries the material; the
                // per-material colour lands with the multi-texture work.
                _stageEffect.Parameters["MaterialDiffuse"]?.SetValue(Vector4.One);
            }
            else
            {
                _effect.Texture = texture;
            }

            foreach (var pass in stageEffect.CurrentTechnique.Passes)
            {
                pass.Apply();
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
        DrawObjects();
        DrawRings();
        DrawPlayer();
        base.Draw(gameTime);

        if (ScreenshotPath is not null && ++_frames >= ScreenshotFrame)
        {
            SaveScreenshot(ScreenshotPath);
            Exit();
        }
    }
}
