using System.Buffers.Binary;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// A model with its lists resolved: vertices, indices, nodes, materials and
/// texture names, bound together through the mesh sets.
/// </summary>
public sealed class NnModel
{
    private NnModel(NnFile file, NnObject header)
    {
        File = file;
        Header = header;
    }

    public NnFile File { get; }
    public NnObject Header { get; }

    public IReadOnlyList<NnVertexList> VertexLists { get; private set; } = [];
    public IReadOnlyList<NnPrimitiveList> PrimitiveLists { get; private set; } = [];
    public IReadOnlyList<NnMeshSet> MeshSets { get; private set; } = [];
    public IReadOnlyList<NnNode> Nodes { get; private set; } = [];
    public IReadOnlyList<NnMaterial> Materials { get; private set; } = [];
    public IReadOnlyList<string> TextureNames { get; private set; } = [];

    public static NnModel? Load(ReadOnlyMemory<byte> data)
    {
        var file = NnFile.Parse(data);
        var header = file.ReadObject();
        if (header is null) return null;

        var model = new NnModel(file, header);
        var span = file.Data;

        model.VertexLists = ReadPointerArray(data, header.VertexListOffset,
            header.VertexListCount, NnVertexList.Size, NnVertexList.Parse);
        model.PrimitiveLists = ReadPointerArray(data, header.PrimitiveListOffset,
            header.PrimitiveListCount, NnPrimitiveList.Size, NnPrimitiveList.Parse);

        var nodes = new List<NnNode>(header.NodeCount);
        for (int i = 0; i < header.NodeCount; i++)
        {
            int at = NnFile.DataBase + header.NodeOffset + i * NnNode.Size;
            if (at + NnNode.Size > span.Length)
                throw new NnException($"node {i} outside the file");
            nodes.Add(NnNode.Parse(span, at));
        }
        model.Nodes = nodes;

        // Mesh sets live under the subobject list, and that is what actually
        // binds a vertex list to a primitive list and a material.
        var meshSets = new List<NnMeshSet>();
        for (int i = 0; i < header.SubObjectCount; i++)
        {
            int at = NnFile.DataBase + header.SubObjectOffset + i * 0x14;
            if (at + 0x14 > span.Length)
                throw new NnException($"subobject {i} outside the file");
            int meshCount = BinaryPrimitives.ReadInt32LittleEndian(span[(at + 4)..]);
            int meshOffset = BinaryPrimitives.ReadInt32LittleEndian(span[(at + 8)..]);
            if (meshOffset == 0) continue;
            for (int j = 0; j < meshCount; j++)
            {
                int m = NnFile.DataBase + meshOffset + j * NnMeshSet.Size;
                if (m + NnMeshSet.Size > span.Length)
                    throw new NnException($"mesh set {j} of subobject {i} outside the file");
                meshSets.Add(NnMeshSet.Parse(span, m));
            }
        }
        model.MeshSets = meshSets;

        var relocations = file.ReadRelocations();
        var materials = new List<NnMaterial>(header.MaterialCount);
        for (int i = 0; i < header.MaterialCount; i++)
        {
            int at = NnFile.DataBase + header.MaterialOffset + i * 8;
            if (at + 8 > span.Length)
                throw new NnException($"material pointer {i} outside the file");
            uint flags = BinaryPrimitives.ReadUInt32LittleEndian(span[at..]);
            int offset = BinaryPrimitives.ReadInt32LittleEndian(span[(at + 4)..]);
            if (NnFile.DataBase + offset + 0x1C > span.Length)
                throw new NnException($"material {i} descriptor outside the file");
            materials.Add(NnMaterial.Parse(span, offset, flags, relocations));
        }
        model.Materials = materials;
        model.TextureNames = file.ReadTextureNames();
        return model;
    }

    /// <summary>The texture a mesh set draws with, or null when untextured.</summary>
    public string? TextureFor(NnMeshSet mesh)
    {
        if (mesh.MaterialIndex < 0 || mesh.MaterialIndex >= Materials.Count) return null;
        return Resolve(Materials[mesh.MaterialIndex].TextureIndex);
    }

    /// <summary>
    /// Every texture a mesh set binds, by the role its material gives it.
    /// </summary>
    /// <remarks>
    /// This is what lets the renderer draw the 1,322 environment maps it used to
    /// discard: a batch keys on the whole set rather than on the base map alone.
    /// Roles come from the stage flag word and are verified against the texture
    /// names — see <see cref="TextureRole"/>.
    /// </remarks>
    public MaterialTextures TexturesFor(NnMeshSet mesh)
    {
        if (mesh.MaterialIndex < 0 || mesh.MaterialIndex >= Materials.Count)
            return default;

        var material = Materials[mesh.MaterialIndex];
        return new MaterialTextures(
            Resolve(material.TextureIndex),
            Resolve(material.IndexFor(TextureRole.Environment)),
            Resolve(material.IndexFor(TextureRole.Normal)),
            Resolve(material.IndexFor(TextureRole.Specular)));
    }

    private string? Resolve(int? index)
    {
        if (index is null || index < 0 || index >= TextureNames.Count) return null;
        string name = TextureNames[index.Value];
        return name.Length == 0 ? null : name;
    }

    /// <summary>How a mesh set blends, from its material's render state.</summary>
    public MaterialBlend BlendFor(NnMeshSet mesh) =>
        mesh.MaterialIndex >= 0 && mesh.MaterialIndex < Materials.Count
            ? Materials[mesh.MaterialIndex].Blend
            : MaterialBlend.Alpha;

    /// <summary>The colour modulating a mesh set's base map, white by default.</summary>
    public (float R, float G, float B, float A) DiffuseFor(NnMeshSet mesh) =>
        mesh.MaterialIndex >= 0 && mesh.MaterialIndex < Materials.Count
            ? Materials[mesh.MaterialIndex].Diffuse
            : (1f, 1f, 1f, 1f);

    /// <summary>Total triangles across every mesh set, validating indices as it goes.</summary>
    public int CountTriangles()
    {
        int total = 0;
        foreach (var mesh in MeshSets)
        {
            if (mesh.VertexListIndex < 0 || mesh.VertexListIndex >= VertexLists.Count)
                throw new NnException($"vertex list index {mesh.VertexListIndex} out of range");
            if (mesh.PrimitiveListIndex < 0 || mesh.PrimitiveListIndex >= PrimitiveLists.Count)
                throw new NnException($"primitive list index {mesh.PrimitiveListIndex} out of range");

            var prim = PrimitiveLists[mesh.PrimitiveListIndex];
            if (prim.Mode != NnPrimitiveList.TriangleStrip)
                throw new NnException($"unexpected primitive mode {prim.Mode:X}");

            int limit = VertexLists[mesh.VertexListIndex].Count;
            foreach (var (a, b, c) in prim.Triangles())
            {
                if (a >= limit || b >= limit || c >= limit)
                    throw new NnException($"index past the {limit}-vertex list");
                total++;
            }
        }
        return total;
    }

    public int CountVertices()
    {
        int total = 0;
        foreach (var list in VertexLists) total += list.Count;
        return total;
    }

    private static List<T> ReadPointerArray<T>(
        ReadOnlyMemory<byte> data, int listOffset, int count, int size,
        Func<ReadOnlyMemory<byte>, int, T> parse)
    {
        var span = data.Span;
        var result = new List<T>(count);
        for (int i = 0; i < count; i++)
        {
            int at = NnFile.DataBase + listOffset + i * 8;
            if (at + 8 > span.Length)
                throw new NnException($"pointer array entry {i} outside the file");
            int offset = BinaryPrimitives.ReadInt32LittleEndian(span[(at + 4)..]);
            int descriptor = NnFile.DataBase + offset;
            if (descriptor + size > span.Length)
                throw new NnException($"descriptor {i} at {descriptor:X} outside the file");
            result.Add(parse(data, descriptor));
        }
        return result;
    }
}

/// <summary>Every texture one material binds, resolved to names by role.</summary>
/// <remarks>
/// A value type so it can key a draw batch directly: two mesh sets belong in the
/// same batch when they bind the same textures in the same roles.
/// </remarks>
/// <param name="Base">The diffuse map. Null on the 336 untextured materials.</param>
/// <param name="Environment">The reflection map, on 1,322 stages.</param>
/// <param name="Normal">The normal map, on 230 stages.</param>
/// <param name="Specular">The specular map, on 65 stages.</param>
public readonly record struct MaterialTextures(
    string? Base, string? Environment, string? Normal, string? Specular)
{
    /// <summary>How many live textures this material binds.</summary>
    public int Count =>
        (Base is null ? 0 : 1) + (Environment is null ? 0 : 1) +
        (Normal is null ? 0 : 1) + (Specular is null ? 0 : 1);

    /// <summary>Whether anything beyond the base map is bound.</summary>
    public bool IsMultiTexture =>
        Environment is not null || Normal is not null || Specular is not null;
}
