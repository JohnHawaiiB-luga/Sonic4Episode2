using System.Numerics;
using Sonic4Episode2.Core;

namespace Sonic4Episode2.Tests;

public sealed class MapFarCameraTests
{
    [Theory]
    [InlineData(0f, -4480f, 3276275712u, 0u)]
    [InlineData(0f, 0f, 3276275712u, 1101004800u)]
    [InlineData(32640f, 0f, 0u, 1101004800u)]
    [InlineData(16320f, -2240f, 3267887104u, 1092616192u)]
    [InlineData(-1f, -4481f, 3276276114u, 3146926373u)]
    [InlineData(-32640f, -8960f, 3276275712u, 0u)]
    [InlineData(32641f, 1f, 0u, 1101004800u)]
    [InlineData(100000f, 5000f, 0u, 1101004800u)]
    [InlineData(1f, -0.001f, 3276275310u, 1101004798u)]
    [InlineData(32639.998f, -4479.999f, 3078619136u, 915556645u)]
    public void FirstActScrollMatchesPcInstructionResults(float x, float y, uint expectedX, uint expectedY)
    {
        var position = MapFarCamera.FirstActPosition(new Vector2(x, y));
        Assert.Equal(expectedX, BitConverter.SingleToUInt32Bits(position.X));
        Assert.Equal(expectedY, BitConverter.SingleToUInt32Bits(position.Y));
        Assert.Equal(160f, position.Z);
    }

    [Fact]
    public void CloudAndGodrayTranslationUsesTheMappedCameraAndSettingsOrigin()
    {
        var camera = MapFarCamera.FirstActPosition(new Vector2(16320, -2240));
        Assert.Equal(new Vector3(100, 10, 0), MapFarCamera.FirstActFollowOffset(camera));
    }

    [Theory]
    [InlineData(float.NaN, 0)]
    [InlineData(0, float.PositiveInfinity)]
    [InlineData(float.NegativeInfinity, 0)]
    [InlineData(16777218f, 0)]
    public void RejectsInvalidCameraCoordinates(float x, float y)
        => Assert.Throws<ArgumentOutOfRangeException>(() => MapFarCamera.FirstActPosition(new Vector2(x, y)));

    [Fact]
    public void RejectsUnknownBackgroundInputs()
        => Assert.Throws<InvalidDataException>(() => MapFarCamera.ValidateFirstAct([1, 2, 3], [4, 5, 6]));
}
