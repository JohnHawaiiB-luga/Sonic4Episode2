using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class ObjectCatalogTests
{
    [Fact]
    public void CoversEveryIdTheDispatchTableFills()
    {
        Assert.Equal(714, ObjectCatalog.All.Count);
        Assert.All(ObjectCatalog.All, e => Assert.InRange(e.Id, 0, 802));
    }

    [Fact]
    public void IdsAreUniqueAndSorted()
    {
        var ids = ObjectCatalog.All.Select(e => e.Id).ToArray();
        Assert.Equal(ids.Distinct().Count(), ids.Length);
        Assert.Equal(ids.OrderBy(i => i), ids);
    }

    [Fact]
    public void NamedObjectsCarryTheirAssetName()
    {
        // Spot checks on names read straight out of the handler body; these
        // would break first if the table were misaligned against the .exe.
        Assert.Equal("WaterSlider", ObjectCatalog.NameOf(132));
        Assert.Equal("Gear", ObjectCatalog.NameOf(182));
        Assert.Equal("CandleStick", ObjectCatalog.NameOf(312));
        Assert.Equal("Warning", ObjectCatalog.NameOf(368));
    }

    [Fact]
    public void EngineClassesComeFromTheSymbolizedBuild()
    {
        // The two anchors: these ids were identified from placement statistics
        // alone, and the linker's own names landed on exactly them.
        Assert.Equal("Start", ObjectCatalog.ClassOf(443));
        Assert.Equal("GoalPanel", ObjectCatalog.ClassOf(520));

        // What beat 52 guessed was a checkpoint is the Red Star Ring.
        Assert.Equal("RedRing", ObjectCatalog.ClassOf(719));

        Assert.Equal(679, ObjectCatalog.All.Count(e => e.Class is not null));
    }

    [Fact]
    public void ClassAndAssetNameDescribeDifferentThings()
    {
        // Where both exist they agree in meaning, not in wording: the object
        // loads a CandleStick and its class is LightMask. Matching behaviours on
        // the asset name is what put springs on an id the game never places.
        Assert.Equal("CandleStick", ObjectCatalog.NameOf(312));
        Assert.Equal("LightMask", ObjectCatalog.ClassOf(312));

        // ...and sometimes they agree outright, which is the cross-check.
        Assert.Equal("WaterSlider", ObjectCatalog.NameOf(132));
        Assert.Equal("WaterSlider", ObjectCatalog.ClassOf(132));
    }

    [Fact]
    public void SpringsAndDashPanelsAreTheRecoveredIdsNotTheGuessedOnes()
    {
        // Springs: ten consecutive variants plus two strays, none of them the
        // id 295 the asset-name scrape used to claim.
        Assert.All(Enumerable.Range(70, 10), i => Assert.True(ObjectCatalog.Is(i, "Spring")));
        Assert.False(ObjectCatalog.Is(295, "Spring"));

        Assert.All(Enumerable.Range(93, 4), i => Assert.True(ObjectCatalog.Is(i, "DashPanel")));
        // 63-67 carried the asset name "Speed" but are item monitors.
        Assert.Equal("Item", ObjectCatalog.ClassOf(63));

        Assert.NotEmpty(ObjectCatalog.IdsOfClass("Spring"));
        Assert.NotEmpty(ObjectCatalog.IdsOfClass("DashPanel"));
    }

    [Fact]
    public void DirectlyReadNamesAreMarkedApartFromInferredOnes()
    {
        Assert.True(ObjectCatalog.Lookup(132)!.Value.Direct);

        var named = ObjectCatalog.All.Where(e => e.Name is not null).ToArray();
        Assert.Equal(116, named.Length);
        // The inferred majority is the reason Direct exists at all.
        Assert.Equal(27, named.Count(e => e.Direct));
        Assert.All(ObjectCatalog.All.Where(e => e.Direct),
                   e => Assert.NotNull(e.Name));
    }

    [Fact]
    public void VariantsOfOneObjectShareAHandler()
    {
        // 132..139 are eight flavours of the same water slider.
        var family = ObjectCatalog.Family(132).Select(e => e.Id).ToArray();
        Assert.Contains(139, family);
        Assert.Equal(ObjectCatalog.Lookup(132)!.Value.Handler,
                     ObjectCatalog.Lookup(139)!.Value.Handler);
    }

    [Fact]
    public void UnknownIdsDegradeToALabelRatherThanThrowing()
    {
        Assert.False(ObjectCatalog.IsKnown(9999));
        Assert.Null(ObjectCatalog.Lookup(9999));
        Assert.Equal("obj9999", ObjectCatalog.Describe(9999));
    }

    [Fact]
    public void UnnamedObjectsStillDescribeAsTheirFamily()
    {
        // The most-placed object in the game loads no named asset. It used to
        // fall back to its handler address; the symbolized build says what it
        // actually is - a camera direction hint, which is why it is everywhere.
        Assert.Null(ObjectCatalog.NameOf(715));
        Assert.Equal("CamMoveDirPrio", ObjectCatalog.ClassOf(715));
        Assert.Equal("CamMoveDirPrio", ObjectCatalog.Describe(715));

        // G_ZONEF places ids 669-671, but neither build's dispatch table fills
        // those slots, so they stay not recovered on both sides.
        Assert.Null(ObjectCatalog.ClassOf(669));
        Assert.False(ObjectCatalog.IsKnown(669));
        Assert.Equal("obj669", ObjectCatalog.Describe(669));
    }

    [Fact]
    public void InstanceSizesAreEnginePlausible()
    {
        var sized = ObjectCatalog.All.Where(e => e.Size > 0).ToArray();
        Assert.True(sized.Length > 500);
        Assert.All(sized, e => Assert.InRange(e.Size, 64, 1 << 16));
    }
}
