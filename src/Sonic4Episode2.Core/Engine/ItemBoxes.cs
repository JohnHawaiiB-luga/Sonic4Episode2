using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>What an item box gives when broken.</summary>
/// <remarks>
/// The values are Episode II's own config numbers, read from the item-effect
/// dispatcher (<c>0x0053A6C0</c>): it computes <c>config - 1</c>, indexes a jump
/// table at <c>0x0096079A</c>, and branches to the matching effect. See
/// <c>docs/FORMAT-EVENTS.md</c>.
/// </remarks>
public enum ItemType
{
    /// <summary>Shield — <c>GmPlayerItemBarrierSet</c>.</summary>
    Barrier = 1,
    /// <summary>Speed shoes — <c>GmPlayerItemHiSpeedSet</c>.</summary>
    HiSpeed = 2,
    /// <summary>Invincibility — <c>GmPlayerItemInvincibleSet</c>.</summary>
    Invincible = 3,
    /// <summary>Ten rings — <c>GmPlayerItemRing10Set</c>.</summary>
    Ring10 = 4,
    /// <summary>Extra life — <c>GmPlayerItem1UPSet</c>.</summary>
    OneUp = 5,
    /// <summary>The state-dependent monitor (co-op / Super).</summary>
    Special = 6,
}

/// <summary>
/// The item boxes (monitors) of a mounted stage — the <c>Item</c> objects.
/// </summary>
/// <remarks>
/// A box is a trigger; a player entering it breaks the box, once, and yields the
/// power-up it holds.
/// <para>
/// <b>The item each box holds is recovered from Episode II's own code and matches
/// Episode I exactly.</b> <c>GmGmkItemInit</c> (<c>0x00539460</c>) maps the object
/// id to a config value through two jump tables and an array; the effect
/// dispatcher (<c>0x0053A6C0</c>) maps that config to one of five named effects.
/// Following both gives ids 63-67 → speed, invincible, 10 rings, shield, 1-UP —
/// the same order Episode I's <c>GmGmkItem.cs</c> lists, arrived at independently.
/// The mapping is in <see cref="TypeOf"/> and documented in
/// <c>docs/FORMAT-EVENTS.md</c>.
/// </para>
/// <para>
/// <b>What the engine grants:</b> rings and 1-UPs, which it has systems for. The
/// other three — shield, speed shoes, invincibility — are identified but need
/// player power-up subsystems this engine does not have yet, so they break and
/// are reported but grant nothing. The <c>Special</c> monitor (co-op / Super) is
/// likewise deferred; the runtime refinement that produces it is not modelled.
/// </para>
/// </remarks>
public sealed class ItemBoxes
{
    /// <summary>Trigger half-extent in game pixels — one collision cell across.</summary>
    public const float TriggerHalfPixels = 16f;

    /// <summary>Rings a <see cref="ItemType.Ring10"/> monitor gives.</summary>
    public const int RingsFromMonitor = 10;

    private readonly Vector2[] _positions;
    private readonly ItemType[] _types;
    private readonly bool[] _broken;

    public ItemBoxes(IReadOnlyList<Placement> placements)
    {
        var items = placements.Where(p => ObjectCatalog.Is(p.ObjectId, "Item")).ToArray();
        _positions = items
            .Select(p => new Vector2(p.X * PlayerPhysics.WorldPerPixel,
                                     -p.Y * PlayerPhysics.WorldPerPixel))
            .ToArray();
        _types = items.Select(p => TypeOf(p.ObjectId)).ToArray();
        _broken = new bool[_positions.Length];
    }

    /// <summary>
    /// The item an object id holds, from Episode II's id → config → effect chain.
    /// </summary>
    /// <remarks>
    /// This is the static base type. A handful of monitors are refined at runtime
    /// by co-op / Super state (<c>GmGmkItemInit</c> <c>0x00539600</c>), which this
    /// does not model, so those show their base item.
    /// </remarks>
    public static ItemType TypeOf(int objectId)
    {
        // Config value per id, read from GmGmkItemInit's jump tables + array.
        int config = objectId switch
        {
            64 or 459 => 3,   // Invincible
            65 or 455 or 460 => 4,   // Ring10
            66 or 456 or 461 => 1,   // Barrier
            67 or 457 or 462 => 5,   // 1-UP
            63 or 458 => 2,   // HiSpeed
            454 => 3,
            566 or 567 => 6,  // Special
            _ => 2,           // ids that fall through default to HiSpeed's slot
        };
        return (ItemType)config;
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

    /// <summary>The item the box at an index holds.</summary>
    public ItemType TypeAt(int index) => _types[index];

    /// <summary>Whether the box at an index has been broken.</summary>
    public bool IsBroken(int index) => _broken[index];

    /// <summary>
    /// Breaks any unbroken box the player is touching, returning the item types
    /// of the boxes that broke this call. A box breaks once and stays broken.
    /// </summary>
    public IReadOnlyList<ItemType> Check(Vector2 feet)
    {
        float half = TriggerHalfPixels * PlayerPhysics.WorldPerPixel;
        List<ItemType>? broke = null;
        for (int i = 0; i < _positions.Length; i++)
        {
            if (_broken[i]) continue;
            bool inside =
                MathF.Abs(feet.X - _positions[i].X) <= half &&
                MathF.Abs(feet.Y - _positions[i].Y) <= half;
            if (inside)
            {
                _broken[i] = true;
                (broke ??= []).Add(_types[i]);
            }
        }
        return (IReadOnlyList<ItemType>?)broke ?? [];
    }
}
