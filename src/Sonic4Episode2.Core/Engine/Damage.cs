using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>The result of trying to damage the player.</summary>
public enum DamageOutcome
{
    Ignored,
    Hurt,
    Death,
}

/// <summary>A damage transition and the ring count it leaves behind.</summary>
public readonly record struct DamageResult(
    DamageOutcome Outcome,
    int RingsRemaining);

/// <summary>The normal-player damage transition.</summary>
/// <remarks>
/// <c>GmPlySeqInitDamage</c> (arm64 <c>0x005B9368</c>) gives an ordinary player
/// 1.5 px/frame horizontally opposite its facing and 3.0 px/frame upward. The
/// damage collision handler at <c>0x005A382C</c> passes the full current ring
/// count to <c>GmRingDamageSetNum</c> and installs the player table's
/// 180-frame invulnerability time.
/// <para>
/// A ringed hit clears the carried count but does not yet spawn the original's
/// recoverable dropped-ring objects. A ringless hit follows
/// <c>GmPlySeqInitDeath</c> (arm64 <c>0x005B9910</c>), whose launch uses the
/// active player row's jump impulse. Respawn and life consumption remain open.
/// </para>
/// <para>
/// Super and special-state branches use different knockback and ring rules.
/// They are ignored rather than sent through the known-wrong normal branch until
/// their player flags are recovered.
/// </para>
/// </remarks>
public sealed class Damage
{
    /// <summary>Normal horizontal knockback, recovered from Episode II.</summary>
    public const float HorizontalKnockbackPixels = 1.5f;

    /// <summary>Normal upward knockback, recovered from Episode II.</summary>
    public const float VerticalKnockbackPixels = 3.0f;

    public DamageResult Apply(Player player, int rings)
    {
        if (player.IsSuper || player.IsDead || player.InvincibleTimer > 0)
            return new DamageResult(DamageOutcome.Ignored, rings);
        if (rings <= 0)
        {
            player.EnterDeath();
            return new DamageResult(DamageOutcome.Death, 0);
        }

        float scale = PlayerPhysics.WorldPerPixel;
        float horizontal = (player.FacingLeft ? 1f : -1f) *
                           HorizontalKnockbackPixels * scale;
        player.EnterDamage(
            horizontal,
            VerticalKnockbackPixels * scale,
            player.Physics.InvincibleFrames);
        return new DamageResult(DamageOutcome.Hurt, 0);
    }
}
