using System.Buffers.Binary;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class RingTests
{
    /// <summary>One block holding the given local positions.</summary>
    private static byte[] OneBlock(params (byte X, byte Y)[] rings)
    {
        var data = new byte[4 + 4 + 2 + rings.Length * RingPlacements.RecordStride];
        BinaryPrimitives.WriteUInt16LittleEndian(data, 1);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(2), 1);
        BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(4), 8);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(8), (ushort)rings.Length);
        for (int i = 0; i < rings.Length; i++)
        {
            data[10 + i * 2] = rings[i].X;
            data[11 + i * 2] = rings[i].Y;
        }
        return data;
    }

    [Fact]
    public void ARecordIsNothingButAPosition()
    {
        var parsed = RingPlacements.Parse(OneBlock((10, 20), (30, 40)));
        Assert.Equal(2, parsed.Items.Count);
        Assert.Equal(new Ring(10, 20), parsed.Items[0]);
        Assert.Equal(new Ring(30, 40), parsed.Items[1]);
        Assert.Equal(2, RingPlacements.RecordStride);
    }

    [Fact]
    public void BlockOriginsOffsetByTheGridPitch()
    {
        // Two blocks side by side; the second block's local 5 is absolute 261.
        var data = new byte[4 + 8 + (2 + 2) + (2 + 2)];
        BinaryPrimitives.WriteUInt16LittleEndian(data, 2);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(2), 1);
        BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(4), 12);
        BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(8), 16);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(12), 1);
        data[14] = 7; data[15] = 8;
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(16), 1);
        data[18] = 5; data[19] = 6;

        var parsed = RingPlacements.Parse(data);
        Assert.Equal(new Ring(7, 8), parsed.Items[0]);
        Assert.Equal(new Ring(EventPlacements.BlockPitch + 5, 6), parsed.Items[1]);
    }

    [Fact]
    public void AnEmptyGridIsValidAndYieldsNothing()
    {
        // Cutscene .RG files are exactly this: a grid with no records anywhere.
        var data = new byte[4 + 4 + 2];
        BinaryPrimitives.WriteUInt16LittleEndian(data, 1);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(2), 1);
        BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(4), 8);
        Assert.Empty(RingPlacements.Parse(data).Items);
    }

    [Fact]
    public void AnOverrunningCountIsRejectedRatherThanRead()
    {
        var data = OneBlock((1, 2));
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(8), 9999);
        Assert.Throws<AmbException>(() => RingPlacements.Parse(data));
    }

    [Fact]
    public void AnOffsetOutsideTheFileIsRejected()
    {
        var data = OneBlock((1, 2));
        BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(4), 1 << 20);
        Assert.Throws<AmbException>(() => RingPlacements.Parse(data));
    }

    [Fact]
    public void RingsAreNotObjectsAndHaveNoCatalogEntry()
    {
        // Stated as a test because it was the wrong assumption for a while: the
        // most-placed .EV ids are not rings, and no id is.
        Assert.DoesNotContain(ObjectCatalog.All, e => e.Name == "Ring");
    }
}
