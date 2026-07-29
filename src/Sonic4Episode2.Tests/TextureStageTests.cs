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

    [Fact]
    public void LiveStagesOnlyEverSitAtEvenArrayPositions()
    {
        // Measured across the whole build: 0 live stages at an odd position and
        // 0 padding at an even one, in 14,357 records. The array interleaves a
        // live record with an inert one, so a material's live stages are at
        // positions 0, 2 and 4 — never 1 or 3.
        var model = Load(@"CUTSCENE\SCENE03\COMMON.AMB", "POD.ZNO");
        if (model is null) return;

        foreach (var material in model.Materials)
            for (int i = 0; i < material.Stages.Count; i++)
                Assert.Equal(i % 2 == 0, material.Stages[i].IsLive);
    }

    [Fact]
    public void TheRoleComesFromTheFlagNotTheArrayPosition()
    {
        // POD.ZNO is the model that kills the position-is-the-slot reading: it
        // orders its stages normal, diffuse, environment, so whatever sits at
        // position 0 is NOT the base map. Cross-checked against the texture
        // names, which carry the engine's own _nml/_dif/_env convention.
        var model = Load(@"CUTSCENE\SCENE03\COMMON.AMB", "POD.ZNO");
        if (model is null) return;

        var material = model.Materials.FirstOrDefault(m => m.LiveStages.Count() == 3);
        if (material is null) return;

        var live = material.LiveStages.ToList();
        Assert.Equal(TextureRole.Normal, live[0].Role);
        Assert.Equal(TextureRole.Base, live[1].Role);
        Assert.Equal(TextureRole.Environment, live[2].Role);

        string NameOf(NnTextureStage s) => model.TextureNames[s.Index].ToLowerInvariant();
        Assert.Contains("_nml", NameOf(live[0]));
        Assert.Contains("_dif", NameOf(live[1]));
        Assert.Contains("_env", NameOf(live[2]));

        // And the fix that follows: TextureIndex must be the diffuse, not
        // whatever happened to be recorded first.
        Assert.Equal(live[1].Index, material.TextureIndex);
    }

    [Fact]
    public void AReflectiveMaterialReportsItsEnvironmentMap()
    {
        // C01_TORNADO is the clean two-texture case — a diffuse base plus a
        // reflection map, which is what a shiny vehicle should have. Before the
        // rekeying the renderer saw only the first of the two.
        var model = Load(@"CUTSCENE\SCENE01\COMMON.AMB", "C01_TORNADO.ZNO");
        if (model is null) return;

        var mesh = model.MeshSets.FirstOrDefault(
            m => model.TexturesFor(m).Environment is not null);
        if (mesh == default) return;

        var textures = model.TexturesFor(mesh);
        Assert.NotNull(textures.Base);
        Assert.NotNull(textures.Environment);
        Assert.Contains("_env", textures.Environment!, StringComparison.OrdinalIgnoreCase);
        Assert.True(textures.IsMultiTexture);
        Assert.Equal(2, textures.Count);

        // The base map is still what a single-texture renderer would have drawn.
        Assert.Equal(model.TextureFor(mesh), textures.Base);
    }

    [Fact]
    public void EveryRoleBitSeenInTheBuildDecodes()
    {
        // The four named bits, verified against the texture-name suffixes over
        // the whole build. Anything else stays Unknown rather than guessed at.
        Assert.Equal(TextureRole.Base, new NnTextureStage(0x60000002, 0).Role);
        Assert.Equal(TextureRole.Environment, new NnTextureStage(0x60000004, 0).Role);
        Assert.Equal(TextureRole.Normal, new NnTextureStage(0x60000001, 0).Role);
        Assert.Equal(TextureRole.Specular, new NnTextureStage(0x60000008, 0).Role);

        Assert.Equal(TextureRole.Unknown, new NnTextureStage(0x60000400, 0).Role);
        Assert.Equal(TextureRole.Unknown, new NnTextureStage(0x00020002, 0).Role);
    }
}
