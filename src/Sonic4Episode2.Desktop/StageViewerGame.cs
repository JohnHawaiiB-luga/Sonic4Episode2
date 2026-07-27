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

    private BasicEffect _effect = null!;
    private VertexPositionNormalTexture[] _vertices = [];
    private readonly Dictionary<string, int[]> _batches = [];
    private readonly Dictionary<string, Texture2D> _textures = [];
    private StageBatch? _pending;
    private Texture2D _white = null!;
    private Texture2D _marker = null!;
    private Texture2D _ring = null!;
    private TileMesh? _ringMesh;
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
        _engine = new GameEngine(_content) { ActArchive = _actArchive };

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
                Vector3.Backward,
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
        _effect = new BasicEffect(GraphicsDevice)
        {
            VertexColorEnabled = false,
            TextureEnabled = true,
            LightingEnabled = false,
        };
        _white = new Texture2D(GraphicsDevice, 1, 1);
        _white.SetData(new[] { Color.Gray });
        _marker = new Texture2D(GraphicsDevice, 1, 1);
        _marker.SetData(new[] { new Color(70, 130, 255) });
        _ring = new Texture2D(GraphicsDevice, 1, 1);
        _ring.SetData(new[] { new Color(255, 200, 40) });

        LoadTextures(_actArchive);
        LoadRingModel();
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

        // One draw per texture. DrawUserIndexedPrimitives also has a per-call
        // primitive limit well below an act's triangle count, so each batch is
        // chunked as well.
        const int chunk = 60000 * 3;
        foreach (var pair in _batches)
        {
            _effect.Texture = _textures.TryGetValue(pair.Key.ToUpperInvariant(), out var texture)
                ? texture
                : _white;

            foreach (var pass in _effect.CurrentTechnique.Passes)
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
        DrawRings();
        DrawPlayerMarker();
        base.Draw(gameTime);
    }
}
