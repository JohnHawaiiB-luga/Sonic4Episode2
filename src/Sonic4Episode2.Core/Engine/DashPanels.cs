using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// The dash panels of a mounted stage — the <c>Speed</c> objects.
/// </summary>
/// <remarks>
/// A dash panel is a floor trigger that sets the player's ground speed to a
/// boost value and suspends friction briefly so the boost is not immediately
/// eaten.
/// <para>
/// <b>The boost is 13.5 px/frame, recovered from Episode II's own code.</b>
/// <c>GmPlySeqInitDashPanel</c> (arm64 <c>0x005D2254</c>) indexes a table at
/// <c>0x0096C658</c> by the panel's direction and reads a two-float velocity
/// vector; every one of its eight populated entries has magnitude <b>13.500</b>
/// — the same number Episode I uses, but now read rather than borrowed. The
/// table also gives the real direction set: right, left, up (×2) and down (×2)
/// as <c>(±13.5, 0)</c> / <c>(0, ±13.5)</c> pairs.
/// </para>
/// <para>
/// The no-friction window is still Episode I's <b>12 frames</b> — it is engine
/// timing, not in that table, and not yet traced in Episode II. Flagged.
/// </para>
/// <para>
/// Direction is per-panel, selected in the object by the placement record's flag
/// byte (offset +4), whose bit-to-direction mapping is not yet traced. Until it
/// is, boosting along the player's current travel is correct for the common case
/// — panels are laid down the direction of play — and a reverse-facing gotcha
/// panel will boost the wrong way, visibly rather than subtly.
/// </para>
/// </remarks>
public sealed class DashPanels
{
    /// <summary>
    /// Boost in game pixels per frame. <b>Recovered from Episode II</b>:
    /// every entry of the direction table at <c>0x0096C658</c> read by
    /// <c>GmPlySeqInitDashPanel</c> has this magnitude.
    /// </summary>
    public const float BoostPixels = 13.5f;

    /// <summary>Friction suspension, in frames. Still Episode I's — not recovered.</summary>
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
