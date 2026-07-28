using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core;

/// <summary>
/// Builds a stage's geometry by instancing each tile's model onto the grid.
/// </summary>
/// <remarks>
/// Two facts drive the placement, both established by measurement rather than
/// assumption:
/// <list type="bullet">
/// <item>A grid cell is <b>20 world units</b>. The dominant tile bounding box is
/// exactly 20x20, with multi-cell pieces at 40 and 60.</item>
/// <item>Models carry a <b>fixed authored origin unrelated to placement</b> —
/// tile 32 appears at cells (98,0) through (98,5) reporting the same centre
/// every time, because the tileset was laid out side by side in one authoring
/// scene. Each model is therefore re-centred on its own bounding box before
/// being instanced.</item>
/// </list>
/// This reconstructs the engine's transform rather than reproducing it: the
/// silhouette matches the tile grid exactly, but exactness is unproven.
/// </remarks>
public sealed class StageAssembler
{
    public const float CellSize = 20.0f;

    /// <summary>
    /// Layer suffixes, longest first so a prefix never shadows a longer name.
    /// </summary>
    /// <remarks>
    /// <c>_M1</c> must be tested before <c>_M</c>, or every parallax layer
    /// collapses onto the same depth.
    /// </remarks>
    public static readonly string[] LayerOrder =
        ["_M1", "_M2", "_M3", "_A", "_B", "_N", "_M"];

    /// <summary>Layer suffix to depth, following the parallax ordering.</summary>
    private static readonly Dictionary<string, float> LayerDepth = new()
    {
        ["_A"] = 128f, ["_B"] = -128f, ["_N"] = 256f,
        ["_M"] = -256f, ["_M1"] = -384f, ["_M2"] = -512f, ["_M3"] = -640f,
    };

    private readonly Dictionary<int, TileMesh?> _cache = [];
    private readonly AmbArchive _tileset;

    public StageAssembler(AmbArchive tileset) => _tileset = tileset;

    public int TilesPlaced { get; private set; }
    public int TilesSkipped { get; private set; }

    /// <summary>
    /// Instances every non-empty cell of one layer, appending to the batch.
    /// </summary>
    public void AddLayer(StageGrid grid, string layerSuffix, StageBatch batch,
                         int regionX = 0, int regionY = 0,
                         int regionWidth = int.MaxValue, int regionHeight = int.MaxValue)
    {
        if (grid.Depth != 2) return;
        float depth = LayerDepth.GetValueOrDefault(layerSuffix, 0f);

        for (int y = Math.Max(0, regionY); y < Math.Min(grid.Height, regionY + regionHeight); y++)
        for (int x = Math.Max(0, regionX); x < Math.Min(grid.Width, regionX + regionWidth); x++)
        {
            var tile = grid.Tile(x, y);
            if (tile.IsEmpty) continue;

            var mesh = GetTile(tile.Id);
            if (mesh is null) { TilesSkipped++; continue; }

            // Grid Y grows downward, world Y grows upward.
            batch.Add(mesh, x * CellSize, -y * CellSize, depth);
            TilesPlaced++;
        }
    }

    private TileMesh? GetTile(int id)
    {
        if (_cache.TryGetValue(id, out var cached)) return cached;

        TileMesh? mesh = null;
        if (id >= 0 && id < _tileset.Count)
        {
            try
            {
                var model = NnModel.Load(_tileset.Read(_tileset.Entries[id]));
                if (model is not null && !model.Header.IsLocator)
                    mesh = TileMesh.From(model);
            }
            catch (Exception ex) when (ex is NnException or AmbException)
            {
                mesh = null;
            }
        }
        _cache[id] = mesh;
        return mesh;
    }
}

/// <summary>One tile's geometry, re-centred on its bounding box.</summary>
public sealed class TileMesh
{
    public required float[] Positions { get; init; }      // xyz triples
    public required float[] TexCoords { get; init; }      // uv pairs
    public required int[] Indices { get; init; }
    public required string?[] TriangleTextures { get; init; }
    public required MaterialBlend[] TriangleBlends { get; init; }

    public static TileMesh From(NnModel model) => Build(model, worldMatrices: null);

    /// <summary>
    /// The model's geometry skinned by the matrix palette — a posed frame of a
    /// skeletal animation.
    /// </summary>
    /// <remarks>
    /// Each vertex of a weighted list blends up to four palette matrices: the
    /// stored weights (the last is one minus their sum), blend indices into the
    /// list's <see cref="NnVertexList.MatrixIndices"/> bone subset, and the
    /// palette built by <see cref="MatrixPalette.Build"/> from
    /// <paramref name="worldMatrices"/>. A list with weights but no per-vertex
    /// indices uses implied indices 0..3 — such lists never carry more than four
    /// bones anywhere in the build. Rigid mesh sets in the same model ride their
    /// node's world matrix, as in <see cref="Posed"/>.
    /// </remarks>
    public static TileMesh Skinned(NnModel model, IReadOnlyList<Matrix4x4> worldMatrices) =>
        Build(model, worldMatrices,
              MatrixPalette.Build(model.Nodes, worldMatrices, model.Header.MatrixPaletteCount));

    /// <summary>
    /// The model's geometry with each mesh set transformed by its node's world
    /// matrix — a posed frame of a rigid animation.
    /// </summary>
    /// <remarks>
    /// A rigid model's mesh sets each ride one node, so posing it is transforming
    /// each mesh set's vertices by <paramref name="worldMatrices"/> at the node it
    /// binds to. The matrices come from <see cref="AnimatedPose.World"/>. This is
    /// how a jet wall rises and a propeller spins; a skinned model needs the
    /// matrix palette instead and is not handled here.
    /// </remarks>
    public static TileMesh Posed(NnModel model, IReadOnlyList<Matrix4x4> worldMatrices) =>
        Build(model, worldMatrices);

    private static TileMesh Build(NnModel model, IReadOnlyList<Matrix4x4>? worldMatrices,
                                  Matrix4x4[]? palette = null)
    {
        var positions = new List<float>();
        var texCoords = new List<float>();
        var indices = new List<int>();
        var textures = new List<string?>();
        var blends = new List<MaterialBlend>();

        float cx = model.Header.CenterX, cy = model.Header.CenterY, cz = model.Header.CenterZ;

        foreach (var mesh in model.MeshSets)
        {
            if (mesh.VertexListIndex < 0 || mesh.VertexListIndex >= model.VertexLists.Count) continue;
            if (mesh.PrimitiveListIndex < 0 || mesh.PrimitiveListIndex >= model.PrimitiveLists.Count) continue;

            var vertexList = model.VertexLists[mesh.VertexListIndex];
            int baseIndex = positions.Count / 3;

            var buffer = new float[vertexList.Count * 3];
            vertexList.ReadPositions(buffer);

            var uvBuffer = new float[vertexList.Count * 2];
            bool hasUv = vertexList.ReadTexCoords(uvBuffer);

            // Skinned: blend palette matrices per vertex. Posed: ride the node's
            // world matrix. Still: re-centre on the bbox, the behaviour From has
            // always had.
            bool skinned = palette is not null && vertexList.IsSkinned &&
                           vertexList.MatrixIndices.Count > 0;
            bool posed = !skinned && worldMatrices is not null &&
                         mesh.NodeIndex >= 0 && mesh.NodeIndex < worldMatrices.Count;
            Matrix4x4 transform = posed ? worldMatrices![mesh.NodeIndex] : Matrix4x4.Identity;

            Span<float> weights = stackalloc float[4];
            Span<byte> bones = stackalloc byte[4];
            for (int i = 0; i < vertexList.Count; i++)
            {
                float x = buffer[i * 3 + 0], y = buffer[i * 3 + 1], z = buffer[i * 3 + 2];
                if (skinned)
                {
                    int stored = vertexList.WeightCount;
                    vertexList.ReadWeights(i, weights);
                    float sum = 0f;
                    for (int k = 0; k < stored; k++) sum += weights[k];
                    weights[stored] = 1f - sum;

                    (bones[0], bones[1], bones[2], bones[3]) = vertexList.BlendIndices(i);
                    var source = new Vector3(x, y, z);
                    var v = Vector3.Zero;
                    for (int k = 0; k <= stored; k++)
                    {
                        if (weights[k] == 0f) continue;
                        int bone = vertexList.HasBlendIndices ? bones[k] : k;
                        if (bone >= vertexList.MatrixIndices.Count) continue;
                        int slot = vertexList.MatrixIndices[bone];
                        if (slot < 0 || slot >= palette!.Length) continue;
                        v += weights[k] * Vector3.Transform(source, palette[slot]);
                    }
                    positions.Add(v.X);
                    positions.Add(v.Y);
                    positions.Add(v.Z);
                }
                else if (posed)
                {
                    var v = Vector3.Transform(new Vector3(x, y, z), transform);
                    positions.Add(v.X);
                    positions.Add(v.Y);
                    positions.Add(v.Z);
                }
                else
                {
                    positions.Add(x - cx);
                    positions.Add(y - cy);
                    positions.Add(z - cz);
                }
                texCoords.Add(hasUv ? uvBuffer[i * 2 + 0] : 0f);
                texCoords.Add(hasUv ? uvBuffer[i * 2 + 1] : 0f);
            }

            string? texture = model.TextureFor(mesh);
            MaterialBlend blend = model.BlendFor(mesh);
            foreach (var (a, b, c) in model.PrimitiveLists[mesh.PrimitiveListIndex].Triangles())
            {
                if (a >= vertexList.Count || b >= vertexList.Count || c >= vertexList.Count) continue;
                indices.Add(baseIndex + a);
                indices.Add(baseIndex + b);
                indices.Add(baseIndex + c);
                textures.Add(texture);
                blends.Add(blend);
            }
        }

        return new TileMesh
        {
            Positions = [.. positions],
            TexCoords = [.. texCoords],
            Indices = [.. indices],
            TriangleTextures = [.. textures],
            TriangleBlends = [.. blends],
        };
    }
}

/// <summary>Accumulated stage geometry in world space, grouped by texture.</summary>
/// <remarks>
/// Grouping happens here rather than in the renderer because a stage draws from
/// dozens of textures and thousands of tiles: sorting once at build time turns
/// what would be a per-tile texture switch into one draw call per texture.
/// </remarks>
public sealed class StageBatch
{
    public List<float> Positions { get; } = [];
    public List<float> TexCoords { get; } = [];
    public List<int> Indices { get; } = [];

    /// <summary>
    /// Index ranges keyed by texture, with additive materials under a key
    /// prefixed <c>+</c>.
    /// </summary>
    /// <remarks>
    /// The prefix keeps the additive triangles in their own draw group so the
    /// renderer can switch to an additive blend state for them — that is what
    /// makes godrays and shine glow rather than sit flat.
    /// </remarks>
    public Dictionary<string, List<int>> IndicesByTexture { get; } = [];

    /// <summary>The additive-blend prefix on a batch key.</summary>
    public const char AdditivePrefix = '+';

    /// <summary>Whether a batch key names an additive-blend group.</summary>
    public static bool IsAdditive(string key) => key.Length > 0 && key[0] == AdditivePrefix;

    /// <summary>The texture name of a batch key, without the blend prefix.</summary>
    public static string TextureOf(string key) => IsAdditive(key) ? key[1..] : key;

    public float MinX { get; private set; } = float.MaxValue;
    public float MaxX { get; private set; } = float.MinValue;
    public float MinY { get; private set; } = float.MaxValue;
    public float MaxY { get; private set; } = float.MinValue;

    public int TriangleCount => Indices.Count / 3;
    public int VertexCount => Positions.Count / 3;

    public void Add(TileMesh mesh, float offsetX, float offsetY, float depth)
    {
        int baseIndex = Positions.Count / 3;
        for (int i = 0, v = 0; i < mesh.Positions.Length; i += 3, v += 2)
        {
            float x = mesh.Positions[i] + offsetX;
            float y = mesh.Positions[i + 1] + offsetY;
            Positions.Add(x);
            Positions.Add(y);
            Positions.Add(mesh.Positions[i + 2] + depth);
            TexCoords.Add(mesh.TexCoords[v]);
            TexCoords.Add(mesh.TexCoords[v + 1]);

            if (x < MinX) MinX = x;
            if (x > MaxX) MaxX = x;
            if (y < MinY) MinY = y;
            if (y > MaxY) MaxY = y;
        }

        for (int t = 0; t < mesh.TriangleTextures.Length; t++)
        {
            string key = mesh.TriangleTextures[t] ?? "";
            if (t < mesh.TriangleBlends.Length && mesh.TriangleBlends[t] == MaterialBlend.Additive)
                key = AdditivePrefix + key;
            if (!IndicesByTexture.TryGetValue(key, out var list))
                IndicesByTexture[key] = list = [];
            for (int k = 0; k < 3; k++)
            {
                int index = baseIndex + mesh.Indices[t * 3 + k];
                list.Add(index);
                Indices.Add(index);
            }
        }
    }
}
