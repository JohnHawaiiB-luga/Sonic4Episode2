using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// The dash panels of a mounted stage — the <c>Speed</c> objects.
/// </summary>
/// <remarks>
/// A dash panel is a floor trigger that sets the player's ground speed to a
/// boost value, in the direction the player is already moving, and suspends
/// friction briefly so the boost is not immediately eaten.
/// <para>
/// <b>The numbers are Episode I's, not recovered.</b> Its panel sets
/// <c>55296</c> FX32 = <b>13.5 px/frame</b> with <c>49152</c> FX32 = <b>12
/// frames</b> of no-friction (<c>GmPlySeqInitDashPanel</c>). Episode II *does*
/// contain 13.5 — one <c>f32</c> and one <c>f64</c> in the whole image,
/// referenced from player-sequence code — which corroborates without proving:
/// the referencing code is curve arithmetic, not a plain speed store. Both
/// values are flagged accordingly.
/// </para>
/// <para>
/// Real panels are directional; the placement flags encode it and their mapping
/// is not recovered. Boosting along the player's current travel is correct for
/// the common case — panels are laid down the direction of play — and wrong for
/// reverse-facing gotcha panels, which will read as boosting the wrong way and
/// be obvious rather than subtle.
/// </para>
/// </remarks>
public sealed class DashPanels
{
    /// <summary>Episode I's boost, game pixels per frame. Not recovered from Episode II.</summary>
    public const float BoostPixels = 13.5f;

    /// <summary>Episode I's friction suspension, in frames. Not recovered from Episode II.</summary>
    public const int NoFrictionFrames = 12;

    /// <summary>Trigger half-extent in game pixels.</summary>
    public const float TriggerHalfPixels = 16f;

    private readonly Vector2[] _positions;
    private readonly bool[] _inside;

    public DashPanels(IReadOnlyList<Placement> placements)
    {
        _positions = placements
            .Where(p => ObjectCatalog.Is(p.ObjectId, "DashPanel"))
            .Select(p => new Vector2(p.X * PlayerPhysics.WorldPerPixel,
                                     -p.Y * PlayerPhysics.WorldPerPixel))
            .ToArray();
        _inside = new bool[_positions.Length];
    }

    public int Count => _positions.Length;
    public Vector2 PositionOf(int index) => _positions[index];

    /// <summary>
    /// Fires any panel the player has just entered; the boost in world units per
    /// frame, or null.
    /// </summary>
    public float? Check(Vector2 feet)
    {
        float half = TriggerHalfPixels * PlayerPhysics.WorldPerPixel;
        float? boost = null;
        for (int i = 0; i < _positions.Length; i++)
        {
            bool inside =
                MathF.Abs(feet.X - _positions[i].X) <= half &&
                MathF.Abs(feet.Y - _positions[i].Y) <= half;
            if (inside && !_inside[i])
                boost = BoostPixels * PlayerPhysics.WorldPerPixel;
            _inside[i] = inside;
        }
        return boost;
    }
}
