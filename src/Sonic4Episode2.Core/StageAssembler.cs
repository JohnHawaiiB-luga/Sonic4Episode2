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
    private readonly Dictionary<int, TileMesh?> _nativeCache = [];
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

    public void AddLayer(StageGrid grid, StageSceneData scene, StageBatch batch)
    {
        if (grid.Depth != 2 || grid.Width != scene.Width || grid.Height != scene.Height ||
            _tileset.Count != scene.ModelCount ||
            !string.Equals(Path.GetFileNameWithoutExtension(grid.Name), scene.Layer,
                           StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("native scene does not match the stage or model archive");
        foreach (var instance in scene.Instances)
        {
            var mesh = GetTile(instance.Model, native: true);
            if (mesh is null)
            {
                var model = NnModel.Load(_tileset.Read(_tileset.Entries[instance.Model]));
                if (model is null || !model.Header.IsLocator)
                    throw new InvalidDataException("native scene selected an unreadable model");
                TilesSkipped++;
                continue;
            }
            batch.Add(mesh, instance.Matrix, instance.X * CellSize);
            TilesPlaced++;
        }
    }

    private TileMesh? GetTile(int id, bool native = false)
    {
        var cache = native ? _nativeCache : _cache;
        if (cache.TryGetValue(id, out var cached)) return cached;

        TileMesh? mesh = null;
        if (id >= 0 && id < _tileset.Count)
        {
            try
            {
                var model = NnModel.Load(_tileset.Read(_tileset.Entries[id]));
                if (model is not null && !model.Header.IsLocator)
                    mesh = native ? TileMesh.ForNativeStage(model) : TileMesh.From(model);
            }
            catch (Exception ex) when (ex is NnException or AmbException)
            {
                mesh = null;
            }
        }
        cache[id] = mesh;
        return mesh;
    }
}

/// <summary>One tile's geometry, re-centred on its bounding box.</summary>
public sealed class TileMesh
{
    public required float[] Positions { get; init; }      // xyz triples
    public required float[] TexCoords { get; init; }      // uv pairs
    public required int[] Indices { get; init; }

    /// <summary>The material each triangle draws with — its whole texture set.</summary>
    public required MaterialKey[] TriangleMaterials { get; init; }

    /// <summary>
    /// Per-vertex normals, xyz triples. Zero-length when the model carries none.
    /// </summary>
    /// <remarks>
    /// The renderer used a constant forward normal until these were plumbed
    /// through, so every surface caught identical light and the stage read flat.
    /// </remarks>
    public required float[] Normals { get; init; }

    public byte[] Colors { get; init; } = [];

    public static TileMesh From(NnModel model) => Build(model, worldMatrices: null);

    public static TileMesh ForRigidModel(NnModel model)
    {
        int count = model.Header.MatrixPaletteCount;
        if (count <= 0 || count > model.Nodes.Count || !NodeTransforms.IsWellFormed(model.Nodes) ||
            model.VertexLists.Any(list => list.IsSkinned) ||
            model.Nodes.Where((node, index) => node.Parent >= index ||
                node.MatrixIndex < -1 || node.MatrixIndex >= count ||
                (node.Flags & 0x1C3F00u) != 0).Any() ||
            model.Nodes.Where(node => node.MatrixIndex >= 0).Select(node => node.MatrixIndex)
                .Distinct().Count() != count ||
            model.Nodes.Count(node => node.MatrixIndex >= 0) != count ||
            model.MeshSets.Any(mesh => mesh.NodeIndex < 0 || mesh.NodeIndex >= model.Nodes.Count ||
                mesh.MatrixIndex < 0 || mesh.MatrixIndex >= count ||
                model.Nodes[mesh.NodeIndex].MatrixIndex != mesh.MatrixIndex))
            throw new InvalidDataException("background requires supported rigid model bindings");
        var palette = MatrixPalette.Build(model.Nodes, NodeTransforms.World(model.Nodes), count);
        return Build(model, worldMatrices: null, rigidPalette: palette);
    }

    public static TileMesh ForNativeStage(NnModel model)
    {
        if (model.Nodes.Count != 1 || !NodeTransforms.IsWellFormed(model.Nodes) ||
            model.VertexLists.Any(list => list.IsSkinned) ||
            model.Header.MatrixPaletteCount != 1 || model.Nodes[0].MatrixIndex != 0 ||
            model.MeshSets.Any(mesh => mesh.NodeIndex != 0 || mesh.MatrixIndex != 0) ||
            (model.Nodes[0].Flags & 0x1C3F00u) != 0)
            throw new InvalidDataException("native preview requires a supported single-node rigid stage model");
        var palette = MatrixPalette.Build(model.Nodes, NodeTransforms.World(model.Nodes), 1);
        return Build(model, worldMatrices: null, rigidPalette: palette);
    }

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
                                  Matrix4x4[]? palette = null, Matrix4x4[]? rigidPalette = null)
    {
        var positions = new List<float>();
        var texCoords = new List<float>();
        var normals = new List<float>();
        var colors = new List<byte>();
        var indices = new List<int>();
        var materials = new List<MaterialKey>();

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

            var nrmBuffer = new float[vertexList.Count * 3];
            bool hasNormals = vertexList.ReadNormals(nrmBuffer);

            var colorBuffer = new byte[vertexList.Count * 4];
            vertexList.ReadDiffuseColors(colorBuffer);

            // Skinned: blend palette matrices per vertex. Posed: ride the node's
            // world matrix. Still: re-centre on the bbox, the behaviour From has
            // always had.
            bool skinned = palette is not null && vertexList.IsSkinned &&
                           vertexList.MatrixIndices.Count > 0;
            bool posed = rigidPalette is not null || (!skinned && worldMatrices is not null &&
                         mesh.NodeIndex >= 0 && mesh.NodeIndex < worldMatrices.Count);
            Matrix4x4 transform = rigidPalette is not null ? rigidPalette[mesh.MatrixIndex] :
                                  posed ? worldMatrices![mesh.NodeIndex] : Matrix4x4.Identity;

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
                int color = i * 4;
                colors.Add(colorBuffer[color]);
                colors.Add(colorBuffer[color + 1]);
                colors.Add(colorBuffer[color + 2]);
                colors.Add(colorBuffer[color + 3]);

                // Normals ride the same transform as the position, minus the
                // translation — a posed or skinned mesh must not keep bind-pose
                // normals or its lighting stays stuck to the rest pose.
                var n = hasNormals
                    ? new Vector3(nrmBuffer[i * 3], nrmBuffer[i * 3 + 1], nrmBuffer[i * 3 + 2])
                    : Vector3.UnitZ;
                if (posed) n = Vector3.TransformNormal(n, transform);
                if (n.LengthSquared() > 1e-12f) n = Vector3.Normalize(n);
                normals.Add(n.X);
                normals.Add(n.Y);
                normals.Add(n.Z);
            }

            var renderState = mesh.MaterialIndex >= 0 && mesh.MaterialIndex < model.Materials.Count
                ? model.Materials[mesh.MaterialIndex].RenderState
                : MaterialRenderState.Default;
            var material = new MaterialKey(model.TexturesFor(mesh), model.BlendFor(mesh),
                                            model.DiffuseFor(mesh))
            {
                RenderState = renderState,
            };
            foreach (var (a, b, c) in model.PrimitiveLists[mesh.PrimitiveListIndex].Triangles())
            {
                if (a >= vertexList.Count || b >= vertexList.Count || c >= vertexList.Count) continue;
                indices.Add(baseIndex + a);
                indices.Add(baseIndex + b);
                indices.Add(baseIndex + c);
                materials.Add(material);
            }
        }

        return new TileMesh
        {
            Positions = [.. positions],
            TexCoords = [.. texCoords],
            Normals = [.. normals],
            Colors = [.. colors],
            Indices = [.. indices],
            TriangleMaterials = [.. materials],
        };
    }
}

/// <summary>What one draw batch draws with: a texture set and a blend mode.</summary>
/// <remarks>
/// <para>
/// The engine's own renderer binds <b>up to four sampler slots</b> and the frame
/// capture caught it using three nested configurations. The slot <i>numbers</i>
/// are not reproduced here on purpose: reading every shader's <c>CTAB</c> shows
/// the register a texture lands in is a property of the shader permutation, not
/// of the texture — <c>s_texBase</c> sits at s0 in 582 shaders and s1 in 207,
/// while <c>s_texNormal</c> is pinned at s0 in all 215 that declare it. So the
/// durable fact is the <b>set of roles</b> a material binds, and our own effect
/// is free to assign its own registers.
/// </para>
/// <para>
/// Blend rides in the key because it is per material and has to split batches
/// exactly as the texture set does — that is what keeps godrays and shine
/// glowing rather than sitting flat.
/// </para>
/// </remarks>
/// <param name="Textures">Every texture the material binds, by role.</param>
/// <param name="Blend">How it blends over what is already drawn.</param>
/// <param name="Diffuse">
/// The colour modulating the base map. White on 87.6% of materials, but its
/// <b>alpha carries per-material transparency</b> — 0.0, 0.25, 0.41, 0.6 and 0.98
/// all occur — so it has to split batches alongside the textures rather than
/// being applied uniformly.
/// </param>
public readonly record struct MaterialKey(
    MaterialTextures Textures, MaterialBlend Blend,
    (float R, float G, float B, float A) Diffuse)
{
    public MaterialRenderState RenderState { get; init; } = MaterialRenderState.Default;

    /// <summary>The diffuse map, or null when the material is untextured.</summary>
    public string? Base => Textures.Base;

    /// <summary>The reflection map, or null.</summary>
    public string? Environment => Textures.Environment;

    /// <summary>The normal map, or null.</summary>
    public string? Normal => Textures.Normal;

    /// <summary>The specular map, or null.</summary>
    public string? Specular => Textures.Specular;

    /// <summary>Whether this batch wants the additive blend state.</summary>
    public bool IsAdditive => Blend == MaterialBlend.Additive;

    /// <summary>Whether anything beyond the base map is bound.</summary>
    public bool IsMultiTexture => Textures.IsMultiTexture;

    /// <summary>A key binding one texture with ordinary transparency.</summary>
    public static MaterialKey FromBase(string? name) =>
        new(new MaterialTextures(name, null, null, null), MaterialBlend.Alpha, White);

    /// <summary>"Show the texture unchanged", which is what 87.6% ask for.</summary>
    public static (float R, float G, float B, float A) White => (1f, 1f, 1f, 1f);
}

/// <summary>Accumulated stage geometry in world space, grouped by material.</summary>
/// <remarks>
/// Grouping happens here rather than in the renderer because a stage draws from
/// dozens of materials and thousands of tiles: sorting once at build time turns
/// what would be a per-tile state switch into one draw call per material.
/// </remarks>
public sealed class StageBatch
{
    public List<float> Positions { get; } = [];
    public List<float> TexCoords { get; } = [];

    /// <summary>Per-vertex normals, xyz triples, parallel to <see cref="Positions"/>.</summary>
    public List<float> Normals { get; } = [];

    public List<byte> Colors { get; } = [];

    public List<int> Indices { get; } = [];

    /// <summary>
    /// Index ranges keyed by the whole material — every texture it binds, plus
    /// its blend mode.
    /// </summary>
    /// <remarks>
    /// <para>
    /// This used to key on a single texture name with a <c>+</c> prefix marking
    /// additive materials. That could only ever draw the base map, which
    /// discarded the 1,322 environment stages and 230 normal maps the models
    /// carry. Keying on the set means a batch is a material, and the renderer can
    /// bind every slot it asks for and pick a technique to match.
    /// </para>
    /// <para>
    /// Grouping still happens here rather than in the renderer, for the same
    /// reason as before: sorting once at build time turns a per-tile state switch
    /// into one draw per material.
    /// </para>
    /// </remarks>
    public Dictionary<MaterialKey, List<int>> IndicesByMaterial { get; } = [];

    /// <summary>
    /// The vertical slice of the stage each triangle belongs to — one entry per
    /// triangle, parallel to <see cref="IndicesByMaterial"/>'s index triples.
    /// </summary>
    /// <remarks>
    /// An act is a long horizontal strip and the camera sees a narrow window of
    /// it, so recording which slice a triangle came from is what lets the
    /// renderer skip the rest. It is recorded here rather than derived later
    /// because the tile's world position is known at placement time and
    /// recovering it from vertex positions afterwards would mean scanning
    /// millions of them.
    /// </remarks>
    public Dictionary<MaterialKey, List<int>> ColumnsByMaterial { get; } = [];

    /// <summary>World units per culling column.</summary>
    /// <remarks>
    /// <para>
    /// 128 is a little over six grid cells. The useful floor is set by the view
    /// width rather than by this: a 1280-pixel window at the follow-camera's zoom
    /// spans 800 world units of a stage some 13,000 wide, so no column size can
    /// beat about 6% of the act and 128 already reaches roughly 7%. Smaller
    /// columns only add span bookkeeping, which is metadata rather than draws.
    /// </para>
    /// <para>
    /// <b>Measured honestly: culling buys nothing on the desktop renderer.</b> It
    /// cuts Zone F from 3,868,937 triangles to 483,268 (12.5%) and is pixel-exact
    /// against an uncalled render, but frame time is unchanged — 25.2 ms against
    /// 24.5 ms, i.e. inside the noise — because the draw was never the
    /// bottleneck. It is kept because this port targets phones, where an eightfold
    /// cut in submitted geometry should matter far more than it does on a desktop
    /// GPU. Whether it actually does is <b>OPEN</b> until it runs on a device.
    /// </para>
    /// </remarks>
    public const float ColumnWidth = 128f;

    /// <summary>The culling column a world X falls in.</summary>
    public static int Column(float worldX) =>
        (int)MathF.Floor(worldX / ColumnWidth);

    public float MinX { get; private set; } = float.MaxValue;
    public float MaxX { get; private set; } = float.MinValue;
    public float MinY { get; private set; } = float.MaxValue;
    public float MaxY { get; private set; } = float.MinValue;

    public int TriangleCount => Indices.Count / 3;
    public int VertexCount => Positions.Count / 3;

    public void Add(TileMesh mesh, float offsetX, float offsetY, float depth)
        => Add(mesh, offsetX, offsetY, depth, null, offsetX);

    public void Add(TileMesh mesh, Matrix4x4 transform, float anchorX)
        => Add(mesh, 0f, 0f, 0f, transform, anchorX);

    private void Add(TileMesh mesh, float offsetX, float offsetY, float depth,
                     Matrix4x4? transform, float anchorX)
    {
        int baseIndex = Positions.Count / 3;
        for (int i = 0, v = 0, color = 0; i < mesh.Positions.Length; i += 3, v += 2, color += 4)
        {
            var position = transform is { } matrix
                ? Vector3.Transform(new Vector3(mesh.Positions[i], mesh.Positions[i + 1], mesh.Positions[i + 2]), matrix) * StageSceneData.DisplayScale
                : new Vector3(mesh.Positions[i] + offsetX, mesh.Positions[i + 1] + offsetY, mesh.Positions[i + 2] + depth);
            float x = position.X;
            float y = position.Y;
            Positions.Add(x);
            Positions.Add(y);
            Positions.Add(position.Z);
            TexCoords.Add(mesh.TexCoords[v]);
            TexCoords.Add(mesh.TexCoords[v + 1]);
            if (color + 4 <= mesh.Colors.Length)
            {
                Colors.Add(mesh.Colors[color]);
                Colors.Add(mesh.Colors[color + 1]);
                Colors.Add(mesh.Colors[color + 2]);
                Colors.Add(mesh.Colors[color + 3]);
            }
            else
            {
                Colors.Add(byte.MaxValue);
                Colors.Add(byte.MaxValue);
                Colors.Add(byte.MaxValue);
                Colors.Add(byte.MaxValue);
            }

            if (transform is { } normalMatrix)
            {
                var normal = i + 2 < mesh.Normals.Length
                    ? new Vector3(mesh.Normals[i], mesh.Normals[i + 1], mesh.Normals[i + 2])
                    : Vector3.UnitZ;
                normal = Vector3.TransformNormal(normal, normalMatrix);
                if (normal.LengthSquared() > 1e-12f) normal = Vector3.Normalize(normal);
                Normals.Add(normal.X);
                Normals.Add(normal.Y);
                Normals.Add(normal.Z);
            }
            else if (i + 2 < mesh.Normals.Length)
            {
                Normals.Add(mesh.Normals[i]);
                Normals.Add(mesh.Normals[i + 1]);
                Normals.Add(mesh.Normals[i + 2]);
            }
            else
            {
                Normals.Add(0f); Normals.Add(0f); Normals.Add(1f);
            }

            if (x < MinX) MinX = x;
            if (x > MaxX) MaxX = x;
            if (y < MinY) MinY = y;
            if (y > MaxY) MaxY = y;
        }

        int column = Column(anchorX);
        for (int t = 0; t < mesh.TriangleMaterials.Length; t++)
        {
            var key = mesh.TriangleMaterials[t];
            if (!IndicesByMaterial.TryGetValue(key, out var list))
            {
                IndicesByMaterial[key] = list = [];
                ColumnsByMaterial[key] = [];
            }
            ColumnsByMaterial[key].Add(column);
            for (int k = 0; k < 3; k++)
            {
                int index = baseIndex + mesh.Indices[t * 3 + k];
                list.Add(index);
                Indices.Add(index);
            }
        }
    }
}
