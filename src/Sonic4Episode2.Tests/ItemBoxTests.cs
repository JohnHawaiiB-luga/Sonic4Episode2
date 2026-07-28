using System.Numerics;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class ItemBoxTests
{
    private static int ItemId { get; } = ObjectCatalog.IdsOfClass("Item").First();

    private static ItemBoxes Field(params (int X, int Y)[] at) =>
        new(at.Select(p => new Placement(p.X, p.Y, ItemId, 0, 0)).ToList());

    [Fact]
    public void OnlyItemPlacementsBecomeBoxes()
    {
        var placements = new List<Placement>
        {
            new(100, 100, ItemId, 0, 0),
            new(200, 100, 9999, 0, 0),          // unknown id
            new(300, 100, 715, 0, 0),           // known id, not an item box
        };
        Assert.Equal(1, new ItemBoxes(placements).Count);
    }

    [Fact]
    public void TouchingABoxBreaksItOnce()
    {
        var boxes = Field((640, 640));
        var at = boxes.PositionOf(0);

        Assert.False(boxes.IsBroken(0));
        Assert.Single(boxes.Check(at));         // breaks, yields one item
        Assert.True(boxes.IsBroken(0));
        Assert.Empty(boxes.Check(at));          // already broken, nothing more
        Assert.Equal(0, boxes.Remaining);
    }

    [Fact]
    public void AMissedBoxStaysWhole()
    {
        var boxes = Field((640, 640));
        Assert.Empty(boxes.Check(boxes.PositionOf(0) + new Vector2(1000f, 0f)));
        Assert.False(boxes.IsBroken(0));
        Assert.Equal(1, boxes.Remaining);
    }

    [Fact]
    public void EachBoxBreaksIndependently()
    {
        var boxes = Field((100, 100), (500, 100));
        Assert.Equal(2, boxes.Remaining);

        Assert.Single(boxes.Check(boxes.PositionOf(0)));
        Assert.True(boxes.IsBroken(0));
        Assert.False(boxes.IsBroken(1));
        Assert.Equal(1, boxes.Remaining);

        Assert.Single(boxes.Check(boxes.PositionOf(1)));
        Assert.Equal(0, boxes.Remaining);
    }

    [Fact]
    public void ItemTypesMatchEpisodeIThroughEpisodeIItsOwnDispatch()
    {
        // ids 63-67 recovered from Episode II's id->config->effect chain, and the
        // order is exactly Episode I's GmGmkItem.cs.
        Assert.Equal(ItemType.HiSpeed, ItemBoxes.TypeOf(63));
        Assert.Equal(ItemType.Invincible, ItemBoxes.TypeOf(64));
        Assert.Equal(ItemType.Ring10, ItemBoxes.TypeOf(65));
        Assert.Equal(ItemType.Barrier, ItemBoxes.TypeOf(66));
        Assert.Equal(ItemType.OneUp, ItemBoxes.TypeOf(67));

        // A second id range resolves to the same effects.
        Assert.Equal(ItemType.Ring10, ItemBoxes.TypeOf(455));
        Assert.Equal(ItemType.OneUp, ItemBoxes.TypeOf(457));
        Assert.Equal(ItemType.HiSpeed, ItemBoxes.TypeOf(458));

        // The config numbers are Episode II's own, straight from the dispatcher.
        Assert.Equal(4, (int)ItemType.Ring10);
        Assert.Equal(5, (int)ItemType.OneUp);
    }

    [Fact]
    public void ABrokenBoxYieldsItsItem()
    {
        var boxes = new ItemBoxes([new Placement(100, 100, 65, 0, 0)]);  // a Ring10 id
        var got = boxes.Check(boxes.PositionOf(0));
        Assert.Equal(ItemType.Ring10, Assert.Single(got));
    }
}
