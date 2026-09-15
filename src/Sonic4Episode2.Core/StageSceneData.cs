using System.Numerics;
using System.Text.Json;

namespace Sonic4Episode2.Core;

public readonly record struct StageSceneInstance(
    int X, int Y, int Model, int Rotation, bool FlipX, bool FlipY, Matrix4x4 Matrix);

public sealed class StageSceneData
{
    public const float DisplayScale = StageAssembler.CellSize / 64f;

    private StageSceneData(string layer, int width, int height, int modelCount,
                           int precision, Vector3 offset, IReadOnlyList<StageSceneInstance> instances)
    {
        Layer = layer;
        Width = width;
        Height = height;
        ModelCount = modelCount;
        Precision = precision;
        Offset = offset;
        Instances = instances;
    }

    public string Layer { get; }
    public int Width { get; }
    public int Height { get; }
    public int ModelCount { get; }
    public int Precision { get; }
    public Vector3 Offset { get; }
    public IReadOnlyList<StageSceneInstance> Instances { get; }

    public static StageSceneData Parse(ReadOnlyMemory<byte> data)
    {
        if (data.Length > 64 * 1024 * 1024)
            throw new InvalidDataException("native scene exceeds the input limit");
        try
        {
            using var document = JsonDocument.Parse(data, new JsonDocumentOptions { MaxDepth = 16 });
            var root = document.RootElement;
            if (root.GetProperty("format").GetString() != "pc-stage-scene-v1")
                throw new InvalidDataException("unsupported native scene format");
            string layer = root.GetProperty("layer").GetString() ?? "";
            if (layer.Length is < 1 or > 64 || layer.Any(c => !char.IsAsciiLetterOrDigit(c) && c != '_'))
                throw new InvalidDataException("invalid native scene layer");
            int width = root.GetProperty("width").GetInt32();
            int height = root.GetProperty("height").GetInt32();
            int modelCount = root.GetProperty("model_count").GetInt32();
            int precision = root.GetProperty("precision").GetInt32();
            if (width is < 1 or > ushort.MaxValue || height is < 1 or > ushort.MaxValue ||
                modelCount is < 1 or > 4096 || precision is not (24 or 53))
                throw new InvalidDataException("invalid native scene dimensions or precision");
            var offsets = root.GetProperty("offset_bits");
            if (offsets.GetArrayLength() != 3)
                throw new InvalidDataException("invalid native scene offsets");
            var offsetValues = offsets.EnumerateArray().Select(ReadFloat).ToArray();
            foreach (float offset in offsetValues)
                if (MathF.Abs(offset) > 16777216f)
                    throw new InvalidDataException("native scene offset is out of range");

            var rows = root.GetProperty("instances");
            if ((long)rows.GetArrayLength() > (long)width * height)
                throw new InvalidDataException("too many native scene instances");
            var instances = new List<StageSceneInstance>(rows.GetArrayLength());
            var anchors = new HashSet<uint>();
            foreach (var row in rows.EnumerateArray())
            {
                int x = row.GetProperty("x").GetInt32();
                int y = row.GetProperty("y").GetInt32();
                int model = row.GetProperty("model").GetInt32();
                int rotation = row.GetProperty("rotation").GetInt32();
                if (x < 0 || x >= width || y < 0 || y >= height || model <= 0 || model >= modelCount ||
                    rotation is not (0 or 16384 or 32768 or 49152) ||
                    !anchors.Add(((uint)x << 16) | (uint)y))
                    throw new InvalidDataException("invalid or duplicated native scene anchor");
                var words = row.GetProperty("matrix_bits");
                if (words.GetArrayLength() != 16)
                    throw new InvalidDataException("invalid native scene matrix length");
                var f = words.EnumerateArray().Select(ReadFloat).ToArray();
                if (f[3] != 0f || f[7] != 0f || f[11] != 0f || f[15] != 1f)
                    throw new InvalidDataException("native scene matrix is not affine");
                var matrix = new Matrix4x4(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7],
                                           f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]);
                instances.Add(new StageSceneInstance(x, y, model, rotation,
                    row.GetProperty("flip_x").GetBoolean(), row.GetProperty("flip_y").GetBoolean(), matrix));
            }
            return new StageSceneData(layer.ToUpperInvariant(), width, height, modelCount,
                                      precision, new Vector3(offsetValues[0], offsetValues[1], offsetValues[2]),
                                      instances.AsReadOnly());
        }
        catch (Exception error) when (error is JsonException or InvalidOperationException or
                                      KeyNotFoundException or FormatException or OverflowException)
        {
            throw new InvalidDataException("malformed native scene data", error);
        }
    }

    private static float ReadFloat(JsonElement word)
    {
        float value = BitConverter.UInt32BitsToSingle(word.GetUInt32());
        if (!float.IsFinite(value))
            throw new InvalidDataException("nonfinite native scene value");
        return value;
    }
}
