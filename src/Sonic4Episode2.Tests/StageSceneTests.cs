using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class StageSceneTests
{
    private static JsonObject Scene(string layer = "ZONE11_A", float depth = 32) => new()
    {
        ["format"] = "pc-stage-scene-v1", ["precision"] = 24, ["layer"] = layer,
        ["width"] = 20, ["height"] = 10, ["model_count"] = 2,
        ["offset_bits"] = JsonSerializer.SerializeToNode(new uint[] { 0, 0, BitConverter.SingleToUInt32Bits(depth) }),
        ["instances"] = new JsonArray(new JsonObject
        {
            ["x"] = 4, ["y"] = 3, ["model"] = 1, ["rotation"] = 16384,
            ["flip_x"] = false, ["flip_y"] = false,
            ["matrix_bits"] = JsonSerializer.SerializeToNode(new float[]
            {
                0, 3.2f, 0, 0, -3.2f, 0, 0, 0, 0, 0, 3.2f, 0, 256, -192, depth, 1
            }.Select(BitConverter.SingleToUInt32Bits).ToArray())
        })
    };

    private static byte[] Bytes(JsonObject scene) => Encoding.UTF8.GetBytes(scene.ToJsonString());

    [Fact]
    public void NativeColumnMatrixRotatesThenTranslatesInDisplayUnits()
    {
        var scene = StageSceneData.Parse(Bytes(Scene()));
        var matrix = Assert.Single(scene.Instances).Matrix;
        var point = Vector3.Transform(new Vector3(2, 3, 4), matrix) * StageSceneData.DisplayScale;
        Assert.Equal(77f, point.X, 5);
        Assert.Equal(-58f, point.Y, 5);
        Assert.Equal(14f, point.Z, 5);
        Assert.Equal(new Vector3(0, 0, 32), scene.Offset);
    }

    [Theory]
    [InlineData("precision", 25)]
    [InlineData("width", 0)]
    [InlineData("height", 65536)]
    [InlineData("model_count", 4097)]
    public void UnsupportedSceneMetadataFails(string field, int value)
    {
        var scene = Scene();
        scene[field] = value;
        Assert.Throws<InvalidDataException>(() => StageSceneData.Parse(Bytes(scene)));
    }

    [Theory]
    [InlineData("x", 20)]
    [InlineData("y", -1)]
    [InlineData("model", 0)]
    [InlineData("model", 2)]
    [InlineData("rotation", 1)]
    public void InvalidPlacementsFail(string field, int value)
    {
        var scene = Scene();
        scene["instances"]![0]![field] = value;
        Assert.Throws<InvalidDataException>(() => StageSceneData.Parse(Bytes(scene)));
    }

    [Theory]
    [InlineData(0, 2139095040u)]
    [InlineData(12, 2143289344u)]
    [InlineData(3, 1065353216u)]
    [InlineData(15, 0u)]
    public void NonfiniteAndProjectiveMatricesFail(int index, uint bits)
    {
        var scene = Scene();
        scene["instances"]![0]!["matrix_bits"]![index] = bits;
        Assert.Throws<InvalidDataException>(() => StageSceneData.Parse(Bytes(scene)));
    }

    [Fact]
    public void DuplicateAnchorsAndMalformedJsonFail()
    {
        var scene = Scene();
        var rows = scene["instances"]!.AsArray();
        rows.Add(rows[0]!.DeepClone());
        Assert.Throws<InvalidDataException>(() => StageSceneData.Parse(Bytes(scene)));
        Assert.Throws<InvalidDataException>(() => StageSceneData.Parse("{"u8.ToArray()));
    }

    [Fact]
    public void RigidPaletteIncludesInverseBindBeforeOuterPlacement()
    {
        var node = new NnNode(0, 0, -1, -1, -1, 5, 7, 11, 0, 0, 0, 1, 1, 1)
        {
            InverseBind = Matrix4x4.CreateTranslation(-5, -7, -11)
        };
        var outer = Matrix4x4.CreateScale(3.2f) * Matrix4x4.CreateTranslation(-16, -86.4f, -35.2f);
        var world = NodeTransforms.World([node]);
        var palette = MatrixPalette.Build([node], world, 1);
        var posed = Vector3.Transform(new Vector3(13, 17, 19), palette[0]);
        var display = Vector3.Transform(posed, outer) * StageSceneData.DisplayScale;
        Assert.Equal(8f, display.X, 5);
        Assert.Equal(-10f, display.Y, 5);
        Assert.Equal(8f, display.Z, 5);
        var nodeOnly = Vector3.Transform(Vector3.Transform(new Vector3(13, 17, 19), world[0]), outer)
                       * StageSceneData.DisplayScale;
        Assert.Equal(13f, nodeOnly.X, 5);
        Assert.Equal(-3f, nodeOnly.Y, 5);
        Assert.Equal(19f, nodeOnly.Z, 5);
    }

    [Fact]
    public void BatchTransformsNormalsAndKeepsCullingAtThePlacementAnchor()
    {
        var mesh = new TileMesh
        {
            Positions = [0, 0, 0, 1, 0, 0, 0, 1, 0], TexCoords = [0, 0, 1, 0, 0, 1],
            Normals = [1, 0, 0, 1, 0, 0, 1, 0, 0], Indices = [0, 1, 2],
            TriangleMaterials = [MaterialKey.FromBase("A.DDS")]
        };
        var matrix = Assert.Single(StageSceneData.Parse(Bytes(Scene())).Instances).Matrix;
        var batch = new StageBatch();
        batch.Add(mesh, matrix, StageBatch.ColumnWidth * 5);
        Assert.Equal(new float[] { 80, -60, 10, 80, -59, 10, 79, -60, 10 }, batch.Positions);
        Assert.Equal(new float[] { 0, 1, 0, 0, 1, 0, 0, 1, 0 }, batch.Normals);
        Assert.Equal([5], batch.ColumnsByMaterial[mesh.TriangleMaterials[0]]);
        var original = new StageBatch();
        original.Add(mesh, 1, 2, 3);
        Assert.Equal(new float[] { 1, 2, 3, 2, 2, 3, 1, 3, 3 }, original.Positions);
        Assert.Equal(mesh.Normals, original.Normals);
    }

    [Theory]
    [InlineData("self-consistent-unknown")]
    [InlineData("map")]
    [InlineData("models")]
    [InlineData("scene")]
    [InlineData("layer")]
    [InlineData("precision")]
    [InlineData("offset")]
    [InlineData("act")]
    [InlineData("path")]
    public void PreviewManifestBindsAssetsScenesAndSupportedInputs(string alteration)
    {
        string directory = Path.Combine(Path.GetTempPath(), "sonic-stage-scene-" + Guid.NewGuid());
        Directory.CreateDirectory(Path.Combine(directory, "G_ZONE1/MAP"));
        try
        {
            string mapPath = Path.Combine(directory, NativeStagePreview.Act);
            string modelPath = Path.Combine(directory, NativeStagePreview.Models);
            File.WriteAllBytes(mapPath, [1]);
            File.WriteAllBytes(modelPath, [2]);
            string Hash(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)));
            var a = Scene();
            if (alteration == "layer") a["layer"] = "ZONE11_B";
            if (alteration == "precision") a["precision"] = 53;
            if (alteration == "offset") a["offset_bits"]![2] = 0;
            string aPath = Path.Combine(directory, "ZONE11_A.json");
            string bPath = Path.Combine(directory, "ZONE11_B.json");
            File.WriteAllBytes(aPath, Bytes(a));
            File.WriteAllBytes(bPath, Bytes(Scene("ZONE11_B", 0)));
            string manifest = Path.Combine(directory, "manifest.json");
            File.WriteAllText(manifest, JsonSerializer.Serialize(new
            {
                format = "pc-stage-preview-v1", act = NativeStagePreview.Act,
                model_archive = NativeStagePreview.Models, map_sha256 = Hash(mapPath),
                model_sha256 = Hash(modelPath), layers = new[]
                {
                    new { file = alteration == "path" ? "../ZONE11_A.json" : "ZONE11_A.json", sha256 = Hash(aPath) },
                    new { file = "ZONE11_B.json", sha256 = Hash(bPath) }
                }
            }));
            if (alteration == "map") File.WriteAllBytes(mapPath, [3]);
            if (alteration == "models") File.WriteAllBytes(modelPath, [4]);
            if (alteration == "scene") File.AppendAllText(aPath, " ");
            string act = alteration == "act" ? "G_ZONE1/MAP/ZONE12_MAP.AMB" : NativeStagePreview.Act;
            var error = Assert.Throws<InvalidDataException>(() => NativeStagePreview.Load(manifest, directory, act));
            if (alteration == "self-consistent-unknown")
                Assert.Contains("unsupported preview fingerprint", error.Message);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }
}
