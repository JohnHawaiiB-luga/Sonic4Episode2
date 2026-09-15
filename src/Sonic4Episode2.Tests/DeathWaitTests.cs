using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class DeathWaitTests
{
    [Fact]
    public void PcTimerPreservesNegativeZeroOnAnEqualClampComparison()
    {
        float negativeZero = BitConverter.UInt32BitsToSingle(0x80000000);
        var result = DeathWait.Tick(new(negativeZero, 3), true, false, negativeZero);
        Assert.Equal(0x80000000u, BitConverter.SingleToUInt32Bits(result.State.Elapsed));
        Assert.Equal(3, result.State.RemainingLives);
        Assert.Equal(DeathWaitAction.None, result.Action);
    }

    [Theory]
    [InlineData(0, -1, DeathWaitAction.GameOver)]
    [InlineData(1, 0, DeathWaitAction.Restart)]
    [InlineData(3, 2, DeathWaitAction.Restart)]
    public void TheCrossingConsumesStockOnceAndZeroStillAllowsARetry(
        int before, int after, DeathWaitAction action)
    {
        var state = new DeathWaitState(0, before);
        for (int i = 0; i < 119; i++)
        {
            var wait = DeathWait.Tick(state, true, false);
            Assert.Equal(DeathWaitAction.None, wait.Action);
            Assert.Equal(before, wait.State.RemainingLives);
            state = wait.State;
        }
        var result = DeathWait.Tick(state, true, false);
        Assert.Equal(new DeathWaitState(120, after), result.State);
        Assert.Equal(action, result.Action);
        Assert.Equal(new DeathWaitResult(result.State, DeathWaitAction.None),
                     DeathWait.Tick(result.State, true, false));
    }

    [Fact]
    public void FractionalTimeCanCrossTheThresholdWithoutLosingTheOvershoot()
    {
        var result = DeathWait.Tick(new(119.75f, 1), true, false, 0.5f);
        Assert.Equal(new DeathWaitState(120.25f, 0), result.State);
        Assert.Equal(DeathWaitAction.Restart, result.Action);
    }

    [Theory]
    [InlineData(false, false)]
    [InlineData(true, true)]
    public void AliveAndSuppressedStatesDoNotAdvance(bool dead, bool suppressed)
    {
        var state = new DeathWaitState(119, 0);
        Assert.Equal(new DeathWaitResult(state, DeathWaitAction.None),
                     DeathWait.Tick(state, dead, suppressed));
    }

    [Fact]
    public void InvalidDomainValuesAreRejectedEvenOnANoop()
    {
        foreach (float invalid in new[] { -1f, float.NaN, float.PositiveInfinity })
        {
            Assert.Throws<ArgumentOutOfRangeException>(() => DeathWait.Tick(new(invalid, 1), false, true));
            Assert.Throws<ArgumentOutOfRangeException>(() => DeathWait.Tick(new(0, 1), false, true, invalid));
        }
        Assert.Throws<ArgumentOutOfRangeException>(() => DeathWait.Tick(new(0, -1), true, false));
        Assert.Throws<ArgumentOutOfRangeException>(() => DeathWait.Tick(new(120, -2), true, false));
        var completed = new DeathWaitState(float.MaxValue, 0);
        Assert.Equal(new DeathWaitResult(completed, DeathWaitAction.None),
                     DeathWait.Tick(completed, true, false, float.MaxValue));
    }
}
