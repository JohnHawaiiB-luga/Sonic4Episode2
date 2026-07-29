namespace Sonic4Episode2.Core.Engine;

public sealed partial class GameEngine
{
    /// <summary>The shared player-damage behaviour.</summary>
    public Damage DamageBehaviour { get; } = new();

    /// <summary>Applies the normal damage transition to the active player.</summary>
    public DamageResult DamagePlayer()
    {
        if (Player is null)
            return new DamageResult(DamageOutcome.Ignored, RingCount);

        DamageResult result = DamageBehaviour.Apply(Player, RingCount);
        RingCount = result.RingsRemaining;
        return result;
    }
}
