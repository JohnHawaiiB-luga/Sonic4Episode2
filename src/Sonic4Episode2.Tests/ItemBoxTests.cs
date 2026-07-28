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
        Assert.Equal(1, boxes.Check(at));       // breaks
        Assert.True(boxes.IsBroken(0));
        Assert.Equal(0, boxes.Check(at));       // already broken, no second break
        Assert.Equal(0, boxes.Remaining);
    }

    [Fact]
    public void AMissedBoxStaysWhole()
    {
        var boxes = Field((640, 640));
        Assert.Equal(0, boxes.Check(boxes.PositionOf(0) + new Vector2(1000f, 0f)));
        Assert.False(boxes.IsBroken(0));
        Assert.Equal(1, boxes.Remaining);
    }

    [Fact]
    public void EachBoxBreaksIndependently()
    {
        var boxes = Field((100, 100), (500, 100));
        Assert.Equal(2, boxes.Remaining);

        Assert.Equal(1, boxes.Check(boxes.PositionOf(0)));
        Assert.True(boxes.IsBroken(0));
        Assert.False(boxes.IsBroken(1));
        Assert.Equal(1, boxes.Remaining);

        Assert.Equal(1, boxes.Check(boxes.PositionOf(1)));
        Assert.Equal(0, boxes.Remaining);
    }
}
