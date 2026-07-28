using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// The springs of a mounted stage — the first object that *does* something.
/// </summary>
/// <remarks>
/// Built from the placements whose catalogue name is <c>Spring</c>. A spring is a
/// trigger box; a player entering it from any direction is launched, and it will
/// not re-fire until the player has left the box, which is what stops one touch
/// reading as sixty.
/// <para>
/// <b>The impulse is Episode I's formula, not a recovered Episode II value.</b>
/// Episode I launches at <c>7.5 + 1.5 * intensity</c> px/frame
/// (<c>GMD_GMK_SPRING_SPD</c> 30720, <c>GMD_GMK_SPRING_SPDAD</c> 6144, FX32).
/// Episode II's spring handler at <c>0x004F7570</c> was read and its reachable
/// constants are timing values — no 7.5 anywhere in it or its callees — so its
/// launch speed comes from somewhere not yet traced. Until it is, this uses the
/// Episode I base with zero intensity, flagged exactly like
/// <see cref="Player.RollThreshold"/>: plausible, oracle-shaped, and not passed
/// off as read.
/// </para>
/// <para>
/// Direction is up-only for now. The placement flags carry 2-bit fields the spawn
/// code reads, and until their mapping to directions is recovered, a wrong guess
/// would fire players into walls.
/// </para>
/// </remarks>
public sealed class Springs
{
    /// <summary>Episode I's base launch speed, game pixels per frame. Not recovered from Episode II.</summary>
    public const float ImpulsePixels = 7.5f;

    /// <summary>Trigger half-extent in game pixels — one collision cell across.</summary>
    public const float TriggerHalfPixels = 16f;

    private readonly Vector2[] _positions;
    private readonly bool[] _inside;

    public Springs(IReadOnlyList<Placement> placements)
    {
        _positions = placements
            .Where(p => ObjectCatalog.Is(p.ObjectId, "Spring"))
            .Select(p => new Vector2(p.X * PlayerPhysics.WorldPerPixel,
                                     -p.Y * PlayerPhysics.WorldPerPixel))
            .ToArray();
        _inside = new bool[_positions.Length];
    }

    public int Count => _positions.Length;

    public Vector2 PositionOf(int index) => _positions[index];

    /// <summary>
    /// Fires any spring the player has just entered, and reports the upward
    /// launch velocity in world units per frame, or null.
    /// </summary>
    /// <param name="feet">The player's position.</param>
    public float? Check(Vector2 feet)
    {
        float half = TriggerHalfPixels * PlayerPhysics.WorldPerPixel;
        float? impulse = null;

        for (int i = 0; i < _positions.Length; i++)
        {
            bool inside =
                MathF.Abs(feet.X - _positions[i].X) <= half &&
                MathF.Abs(feet.Y - _positions[i].Y) <= half;

            if (inside && !_inside[i])
                impulse = ImpulsePixels * PlayerPhysics.WorldPerPixel;
            _inside[i] = inside;
        }
        return impulse;
    }
}
