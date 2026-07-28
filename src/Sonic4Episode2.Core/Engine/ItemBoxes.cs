using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// The item boxes (monitors) of a mounted stage — the <c>Item</c> objects.
/// </summary>
/// <remarks>
/// A box is a trigger; a player entering it breaks the box, once. This models
/// the <b>confirmed</b> half of the behaviour — the break. The power-up it grants
/// is <b>not</b> applied yet, deliberately:
/// <para>
/// The five effects exist and are named in Episode II
/// (<c>GmPlayerItemHiSpeedSet</c> <c>0x005A7CE8</c>, <c>…InvincibleSet</c>
/// <c>0x005A7D80</c>, <c>…Ring10Set</c> <c>0x005A7E50</c>, <c>…BarrierSet</c>
/// <c>0x005A7E88</c>, <c>…1UPSet</c> <c>0x005A7F38</c>), and the box's base item
/// type is recoverable from <c>GmGmkItemInit</c> (see
/// <c>docs/FORMAT-EVENTS.md</c>). But two things stop a faithful grant today:
/// the type is <b>refined at runtime by game state</b> — co-op mode and Super
/// availability change what a box shows — and three of the five effects (speed
/// shoes, shield, invincibility) need player subsystems this engine does not have.
/// Rings and 1-UP it could grant; the rest it cannot, so it grants none rather
/// than a subset that would read as the box giving the wrong thing.
/// </para>
/// <para>
/// Breaking is real behaviour on its own and the foundation the grant will sit
/// on, so it ships now and the effect follows once those subsystems exist.
/// </para>
/// </remarks>
public sealed class ItemBoxes
{
    /// <summary>Trigger half-extent in game pixels — one collision cell across.</summary>
    public const float TriggerHalfPixels = 16f;

    private readonly Vector2[] _positions;
    private readonly bool[] _broken;

    public ItemBoxes(IReadOnlyList<Placement> placements)
    {
        _positions = placements
            .Where(p => ObjectCatalog.Is(p.ObjectId, "Item"))
            .Select(p => new Vector2(p.X * PlayerPhysics.WorldPerPixel,
                                     -p.Y * PlayerPhysics.WorldPerPixel))
            .ToArray();
        _broken = new bool[_positions.Length];
    }

    /// <summary>How many boxes the act placed.</summary>
    public int Count => _positions.Length;

    /// <summary>How many are still unbroken.</summary>
    public int Remaining
    {
        get
        {
            int n = 0;
            foreach (bool b in _broken) if (!b) n++;
            return n;
        }
    }

    public Vector2 PositionOf(int index) => _positions[index];

    /// <summary>Whether the box at an index has been broken.</summary>
    public bool IsBroken(int index) => _broken[index];

    /// <summary>
    /// Breaks any unbroken box the player is touching, and returns how many broke
    /// this call (0 or more). A box breaks once and stays broken.
    /// </summary>
    public int Check(Vector2 feet)
    {
        float half = TriggerHalfPixels * PlayerPhysics.WorldPerPixel;
        int broke = 0;
        for (int i = 0; i < _positions.Length; i++)
        {
            if (_broken[i]) continue;
            bool inside =
                MathF.Abs(feet.X - _positions[i].X) <= half &&
                MathF.Abs(feet.Y - _positions[i].Y) <= half;
            if (inside)
            {
                _broken[i] = true;
                broke++;
            }
        }
        return broke;
    }
}
