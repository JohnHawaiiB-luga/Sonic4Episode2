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
        // The most-placed object in the game loads no named asset, so it falls
        // back to its handler - which still tells it apart from everything else.
        Assert.Null(ObjectCatalog.NameOf(715));
        Assert.StartsWith("obj@", ObjectCatalog.Describe(715));
    }

    [Fact]
    public void InstanceSizesAreEnginePlausible()
    {
        var sized = ObjectCatalog.All.Where(e => e.Size > 0).ToArray();
        Assert.True(sized.Length > 500);
        Assert.All(sized, e => Assert.InRange(e.Size, 64, 1 << 16));
    }
}
