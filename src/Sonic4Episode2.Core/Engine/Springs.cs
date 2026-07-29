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
/// <b>The impulse magnitude is still Episode I's, not recovered.</b> Episode I
/// launches at <c>7.5 + 1.5 * intensity</c> px/frame. In Episode II the spring's
/// touch handler passes the launch velocity into <c>GmPlySeqInitSpringJump</c>
/// (arm64 <c>0x005D1520</c>) as arguments rather than reading a named constant,
/// so the scalar is not a single traceable value. What <i>is</i> recovered from
/// that sequence is the <b>horizontal-velocity ceiling of 9.0</b> px/frame it
/// clamps the result to (<c>0x41100000</c>). The base speed stays flagged.
/// </para>
/// <para>
/// <b>The direction set is recovered.</b> <c>GmGmkSpringInit</c> (arm64
/// <c>0x00563CB0</c>) reads a per-variant A16 angle table at <c>0x00961D34</c>:
/// <c>0, 8192, 16384, 24576, 32768, 40960, 49152, 57344</c> — the eight compass
/// directions in 65536-per-turn units (0°, 45°, 90° … 315°), with a few variants
/// reusing 45°/315°. So springs fire in eight directions, not just up. This
/// engine still launches straight up until the placement flag that selects the
/// variant is wired through, which keeps a wrong guess from firing players into
/// walls; the table it will index is now known.
/// </para>
/// </remarks>
public sealed class Springs
{
    /// <summary>Episode I's base launch speed, game pixels per frame. Not recovered from Episode II.</summary>
    public const float ImpulsePixels = 7.5f;

    /// <summary>
    /// Horizontal-velocity ceiling a spring launch is clamped to, in game pixels
    /// per frame. <b>Recovered from Episode II</b>: <c>GmPlySeqInitSpringJump</c>
    /// caps offset <c>0xC8</c> at this value. The named damage-speed setter at
    /// arm64 <c>0x005B9304</c> independently identifies <c>0xC8</c> as horizontal
    /// by deriving the facing flag from its sign.
    /// </summary>
    public const float HorizontalSpeedCap = 9.0f;

    [Obsolete("Use HorizontalSpeedCap; the recovered field is horizontal.")]
    public const float VerticalSpeedCap = HorizontalSpeedCap;

    /// <summary>
    /// Spring launch directions, A16 angles (65536 per turn), read from the table
    /// at <c>0x00961D34</c> that <c>GmGmkSpringInit</c> indexes by variant.
    /// </summary>
    public static readonly int[] DirectionAngles =
        [0, 8192, 16384, 24576, 32768, 40960, 49152, 57344];

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
