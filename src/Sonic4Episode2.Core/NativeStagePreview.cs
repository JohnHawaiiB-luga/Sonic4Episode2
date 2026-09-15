using System.Numerics;
using System.Security.Cryptography;
using System.Text.Json;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core;

public static class NativeStagePreview
{
    public const string Act = "G_ZONE1/MAP/ZONE11_MAP.AMB";
    public const string Models = "G_ZONE1/MAP/ZONE1_M.AMB";

    public static IReadOnlyDictionary<string, StageSceneData> Load(
        string manifestPath, string gameRoot, string act)
        => Load(manifestPath, gameRoot, act, out _);

    public static IReadOnlyDictionary<string, StageSceneData> Load(
        string manifestPath, string gameRoot, string act, out IContentSource content)
    {
        try
        {
            using var document = JsonDocument.Parse(ReadBounded(manifestPath, 64 * 1024));
            var root = document.RootElement;
            if (root.GetProperty("format").GetString() != "pc-stage-preview-v1" ||
                act.Replace('\\', '/') != Act || root.GetProperty("act").GetString() != Act ||
                root.GetProperty("model_archive").GetString() != Models)
                throw new InvalidDataException("native preview currently supports the first act only");
            var mapBytes = ReadBounded(Path.Combine(gameRoot, Act), 64 * 1024 * 1024);
            var modelBytes = ReadBounded(Path.Combine(gameRoot, Models), 64 * 1024 * 1024);
            string mapHash = CheckHash(mapBytes,
                      root.GetProperty("map_sha256").GetString());
            string modelHash = CheckHash(modelBytes,
                      root.GetProperty("model_sha256").GetString());
            var layers = root.GetProperty("layers");
            if (layers.GetArrayLength() != 2)
                throw new InvalidDataException("native preview requires main layers A and B");
            string directory = Path.GetDirectoryName(Path.GetFullPath(manifestPath))!;
            var scenes = new Dictionary<string, StageSceneData>(StringComparer.Ordinal);
            var sceneHashes = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var layer in layers.EnumerateArray())
            {
                string name = layer.GetProperty("file").GetString() ?? "";
                if (name is not ("ZONE11_A.json" or "ZONE11_B.json"))
                    throw new InvalidDataException("unexpected native preview layer file");
                var bytes = ReadBounded(Path.Combine(directory, name), 64 * 1024 * 1024);
                string sceneHash = CheckHash(bytes, layer.GetProperty("sha256").GetString());
                var scene = StageSceneData.Parse(bytes);
                var offset = name == "ZONE11_A.json" ? new Vector3(0, 0, 32) : Vector3.Zero;
                if (scene.Layer + ".json" != name || scene.Precision != 24 ||
                    scene.Offset != offset || !scenes.TryAdd(scene.Layer, scene))
                    throw new InvalidDataException("native preview layer, precision or initial offset mismatch");
                sceneHashes.Add(scene.Layer, sceneHash);
            }
            var a = scenes["ZONE11_A"];
            var b = scenes["ZONE11_B"];
            if (a.Width != b.Width || a.Height != b.Height || a.ModelCount != b.ModelCount)
                throw new InvalidDataException("native preview layers have different dimensions or model counts");
            if (mapHash != "32FADEBF8B472F06F4EC1B26B5C885992249576CC1A2C8CEEDE4A1A03B50EFD2" ||
                modelHash != "674CDE5A87D3C63E8A54CDA6D4A4A0A46C3EF7D325542003980C9DF1B48FEC80" ||
                sceneHashes["ZONE11_A"] != "0A1AA4579351BEF959161AD385F0E4492000207BC38D524F2505A1335C9E720C" ||
                sceneHashes["ZONE11_B"] != "B7BAF4EB4FA1942EF7E3B457369E465CBA19BF509388828399FBA5C1C432C302")
                throw new InvalidDataException("unsupported preview fingerprint; this preview requires the verified PC first-act inputs and scene exports");
            var background = ReadBounded(Path.Combine(gameRoot, MapFarCamera.FirstActArchive), 64 * 1024 * 1024);
            var settings = ReadBounded(Path.Combine(gameRoot, MapFarCamera.SettingsArchive), 64 * 1024);
            MapFarCamera.ValidateFirstAct(background, settings);
            content = new VerifiedContent(new FileSystemContent(Path.GetFullPath(gameRoot)),
                                          mapBytes, modelBytes, background, settings);
            return scenes.AsReadOnly();
        }
        catch (Exception error) when (error is JsonException or InvalidOperationException or
                                      KeyNotFoundException or FormatException or OverflowException)
        {
            throw new InvalidDataException("malformed native preview manifest", error);
        }
    }

    private sealed class VerifiedContent(IContentSource fallback, byte[] map, byte[] models,
                                         byte[] background, byte[] settings) : IContentSource
    {
        private readonly Dictionary<string, byte[]> _verified = new(StringComparer.OrdinalIgnoreCase)
        {
            [Act] = map, [Models] = models,
            [MapFarCamera.FirstActArchive] = background, [MapFarCamera.SettingsArchive] = settings
        };

        public bool Exists(string path) => _verified.ContainsKey(path.Replace('\\', '/')) || fallback.Exists(path);
        public byte[] Read(string path) => _verified.TryGetValue(path.Replace('\\', '/'), out var bytes)
            ? (byte[])bytes.Clone() : fallback.Read(path);
        public IEnumerable<string> List(string directory, string suffix) => fallback.List(directory, suffix)
            .Concat(_verified.Keys.Where(path =>
                path[..path.LastIndexOf('/')].Equals(directory.Replace('\\', '/').TrimEnd('/'), StringComparison.OrdinalIgnoreCase) &&
                path.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)))
            .Distinct(StringComparer.OrdinalIgnoreCase);
    }

    private static byte[] ReadBounded(string path, int limit)
    {
        using var file = File.OpenRead(path);
        if (file.Length > limit)
            throw new InvalidDataException("native preview input exceeds the size limit");
        var bytes = new byte[checked((int)file.Length)];
        file.ReadExactly(bytes);
        return bytes;
    }

    private static string CheckHash(byte[] bytes, string? expected)
    {
        string actual = Convert.ToHexString(SHA256.HashData(bytes));
        if (expected is null || expected.Length != 64 ||
            !string.Equals(actual, expected,
                           StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("native preview input hash mismatch; regenerate the preview");
        return actual;
    }
}
