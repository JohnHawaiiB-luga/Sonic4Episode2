using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

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
    private readonly string _gameRoot;
    private readonly string _actArchive;

    private BasicEffect _effect = null!;
    private VertexPositionNormalTexture[] _vertices = [];
    private readonly Dictionary<string, int[]> _batches = [];
    private readonly Dictionary<string, Texture2D> _textures = [];
    private StageBatch? _pending;
    private Texture2D _white = null!;

    private Vector2 _camera;
    private float _zoom = 1f;
    private string _status = "";

    public StageViewerGame(string gameRoot, string actArchive)
    {
        _gameRoot = gameRoot;
        _actArchive = actArchive;
        _graphics = new GraphicsDeviceManager(this)
        {
            PreferredBackBufferWidth = 1280,
            PreferredBackBufferHeight = 720,
        };
        IsMouseVisible = true;
        Window.Title = "Sonic 4 Episode II — stage viewer";
    }

    protected override void Initialize()
    {
        LoadStage();
        base.Initialize();
    }

    private void LoadStage()
    {
        string actPath = Path.Combine(_gameRoot, _actArchive);
        var archive = AmbArchive.Load(actPath);

        string? tilesetPath = FindTileset(actPath);
        if (tilesetPath is null)
            throw new InvalidOperationException($"no tileset archive beside {actPath}");

        var assembler = new StageAssembler(AmbArchive.Load(tilesetPath));
        var batch = new StageBatch();

        // The main terrain layer is enough to prove the chain; the parallax
        // layers only add depth complexity to a viewer with no camera model.
        foreach (var entry in archive.Entries)
        {
            string label = entry.Name.Replace('\\', '/');
            label = label[(label.LastIndexOf('/') + 1)..];
            if (!label.EndsWith("_B.MP", StringComparison.OrdinalIgnoreCase)) continue;

            var grid = StageGrid.Parse(label, archive.Read(entry).Span);
            assembler.AddLayer(grid, "_B", batch);
        }

        _pending = batch;
        _status = $"{assembler.TilesPlaced} tiles, {batch.VertexCount:N0} vertices, " +
                  $"{batch.TriangleCount:N0} triangles";
        Console.WriteLine(_status);

        _camera = new Vector2((batch.MinX + batch.MaxX) / 2f, (batch.MinY + batch.MaxY) / 2f);
        float span = Math.Max(batch.MaxX - batch.MinX, 1f);
        _zoom = 1280f / span;
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
        string? directory = Path.GetDirectoryName(actPath);
        if (directory is null) return;

        foreach (string file in Directory.EnumerateFiles(directory, "*.AMB"))
        {
            string upper = Path.GetFileName(file).ToUpperInvariant();
            if (!upper.EndsWith("_T.AMB") && !upper.EndsWith("_TEX.AMB")) continue;

            AmbArchive archive;
            try { archive = AmbArchive.Load(file); }
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

    private static string? FindTileset(string actPath)
    {
        string? directory = Path.GetDirectoryName(actPath);
        if (directory is null) return null;
        string name = Path.GetFileNameWithoutExtension(actPath);

        // ZONE<zone><act>[<tileset>]_MAP -> ZONE<zone>[<tileset>]_M
        if (!name.StartsWith("ZONE", StringComparison.OrdinalIgnoreCase)) return null;
        string body = name[4..].Replace("_MAP", "", StringComparison.OrdinalIgnoreCase);
        if (body.Length < 2) return null;

        string zone = body[..^1];
        string tail = body[^1..];
        string tileset = char.IsDigit(tail[0]) ? "" : tail;
        if (tileset.Length != 0) zone = body[..^2];

        foreach (string candidate in new[] { $"ZONE{zone}{tileset}_M.AMB", $"ZONE{zone}_M.AMB" })
        {
            string path = Path.Combine(directory, candidate);
            if (File.Exists(path)) return path;
        }
        return null;
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

        LoadTextures(Path.Combine(_gameRoot, _actArchive));
        if (_pending is not null)
        {
            BuildBuffers(_pending);
            _pending = null;
        }
    }

    protected override void Update(GameTime gameTime)
    {
        var keyboard = Keyboard.GetState();
        if (keyboard.IsKeyDown(Keys.Escape)) Exit();

        float delta = (float)gameTime.ElapsedGameTime.TotalSeconds;
        float pan = 600f * delta / _zoom;
        if (keyboard.IsKeyDown(Keys.Left)) _camera.X -= pan;
        if (keyboard.IsKeyDown(Keys.Right)) _camera.X += pan;
        if (keyboard.IsKeyDown(Keys.Up)) _camera.Y += pan;
        if (keyboard.IsKeyDown(Keys.Down)) _camera.Y -= pan;
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
        base.Draw(gameTime);
    }
}
