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
    private VertexPositionColor[] _vertices = [];
    private int[] _indices = [];

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

        BuildBuffers(batch);
        _status = $"{assembler.TilesPlaced} tiles, {batch.VertexCount:N0} vertices, " +
                  $"{batch.TriangleCount:N0} triangles";
        Console.WriteLine(_status);

        _camera = new Vector2((batch.MinX + batch.MaxX) / 2f, (batch.MinY + batch.MaxY) / 2f);
        float span = Math.Max(batch.MaxX - batch.MinX, 1f);
        _zoom = 1280f / span;
    }

    private void BuildBuffers(StageBatch batch)
    {
        _vertices = new VertexPositionColor[batch.VertexCount];
        for (int i = 0; i < batch.VertexCount; i++)
        {
            float x = batch.Positions[i * 3];
            float y = batch.Positions[i * 3 + 1];
            float z = batch.Positions[i * 3 + 2];
            // Depth-keyed tint, so overlapping geometry stays readable without
            // textures or lighting.
            float t = Math.Clamp((z + 700f) / 1000f, 0f, 1f);
            var colour = new Color(0.45f + 0.4f * t, 0.40f + 0.35f * t, 0.34f + 0.30f * t);
            _vertices[i] = new VertexPositionColor(new Vector3(x, y, z), colour);
        }
        _indices = [.. batch.Indices];
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
            VertexColorEnabled = true,
            LightingEnabled = false,
        };
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
        if (_indices.Length == 0) return;

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

        foreach (var pass in _effect.CurrentTechnique.Passes)
        {
            pass.Apply();
            // Split into chunks: DrawUserIndexedPrimitives has a per-call limit
            // well below a whole act's triangle count.
            const int chunk = 60000 * 3;
            for (int start = 0; start < _indices.Length; start += chunk)
            {
                int count = Math.Min(chunk, _indices.Length - start);
                GraphicsDevice.DrawUserIndexedPrimitives(
                    PrimitiveType.TriangleList,
                    _vertices, 0, _vertices.Length,
                    _indices, start, count / 3);
            }
        }
        base.Draw(gameTime);
    }
}
