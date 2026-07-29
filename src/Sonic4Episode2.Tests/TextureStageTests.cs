using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

/// <summary>
/// Multi-texture materials, checked against the game's own archives.
/// </summary>
public class TextureStageTests
{
    private const string Root =
        @"C:\Users\DavidErikGarciaArena\Downloads\Sonic 4 - Episode 2 (Beta 8)\Sonic 4 - Episode 2 (Beta 8)";

    private static NnModel? Load(string archive, string stem)
    {
        string path = Path.Combine(Root, archive);
        if (!File.Exists(path)) return null;
        var a = AmbArchive.Load(path);
        foreach (var e in a.Entries)
            if (e.Name.EndsWith(stem, StringComparison.OrdinalIgnoreCase))
                return NnModel.Load(a.Read(e));
        return null;
    }

    [Fact]
    public void ASingleTexturedTileHasOneLiveStage()
    {
        var model = Load(@"G_ZONE1\MAP\ZONE1_M.AMB", "Z1_G_HASIRA_B.ZNO");
        if (model is null) return;

        // Material 0 binds nothing; 1 and 2 each bind one base map, and those
        // two differ - the model that defeated the old subobject approach.
        Assert.Empty(model.Materials[0].Stages);
        foreach (int i in new[] { 1, 2 })
        {
            var stages = model.Materials[i].Stages;
            Assert.Single(stages);
            Assert.True(stages[0].IsBase);
            Assert.Equal(stages[0].Index, model.Materials[i].TextureIndex);
        }
        Assert.NotEqual(model.Materials[1].TextureIndex, model.Materials[2].TextureIndex);
    }

    [Fact]
    public void EveryStageIndexResolvesToARealTexture()
    {
        var model = Load(@"G_ZONE1\MAP\ZONE1_M.AMB", "Z1_G_HASIRA_B.ZNO");
        if (model is null) return;

        foreach (var m in model.Materials)
            foreach (var s in m.Stages)
                Assert.InRange(s.Index, 0, model.TextureNames.Count - 1);
    }

    [Fact]
    public void FlagFamiliesSeparateLiveStagesFromPadding()
    {
        // The padding family mirrors its low half into its high half and leaves
        // the top nibble clear; live stages set it.
        Assert.False(new NnTextureStage(0x00010001, 0).IsLive);
        Assert.False(new NnTextureStage(0x00020002, 0).IsLive);

        var baseMap = new NnTextureStage(0x60000002, 3);
        Assert.True(baseMap.IsLive);
        Assert.True(baseMap.IsBase);
        Assert.False(baseMap.IsEnvironment);

        var env = new NnTextureStage(0x60000004, 2);
        Assert.True(env.IsLive);
        Assert.True(env.IsEnvironment);
        Assert.False(env.IsBase);
    }

    [Fact]
    public void TheBaseStageStillDrivesTextureFor()
    {
        var model = Load(@"G_ZONE1\MAP\ZONE1_M.AMB", "Z1_G_HASIRA_B.ZNO");
        if (model is null) return;

        // Multi-stage decoding must not disturb what already rendered.
        foreach (var mesh in model.MeshSets)
        {
            string? name = model.TextureFor(mesh);
            if (name is not null) Assert.Contains(".dds", name, StringComparison.OrdinalIgnoreCase);
        }
    }
}
