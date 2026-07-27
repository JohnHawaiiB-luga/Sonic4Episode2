using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// The rings of a mounted stage, and which of them have been taken.
/// </summary>
/// <remarks>
/// Ring positions arrive in stage pixels with Y growing downward, the same frame
/// the tile grid uses. World space grows upward and runs at
/// <see cref="StageAssembler.CellSize"/> units per cell, so both axes convert
/// through <see cref="PlayerPhysics.WorldPerPixel"/> and Y flips sign.
/// <para>
/// Collection is a rectangle overlap rather than a distance check, because that
/// is what the original does and the two disagree at the corners. The player box
/// is Episode I's body rectangle and the ring is 16 pixels square.
/// </para>
/// </remarks>
public sealed class RingField
{
    /// <summary>Ring sprite size in game pixels.</summary>
    public const float RingPixels = 16f;

    /// <summary>Player body box in game pixels, from Episode I's player setup.</summary>
    public const float BodyHalfWidth = 8f;
    public const float BodyTop = 19f;
    public const float BodyBottom = 13f;

    private readonly Ring[] _rings;
    private readonly bool[] _taken;

    public RingField(IReadOnlyList<Ring> rings)
    {
        _rings = [.. rings];
        _taken = new bool[_rings.Length];
    }

    public int Count => _rings.Length;
    public int Collected { get; private set; }
    public int Remaining => Count - Collected;

    public Ring this[int index] => _rings[index];
    public bool IsTaken(int index) => _taken[index];

    /// <summary>Where a ring sits in world space.</summary>
    public Vector2 WorldPosition(int index)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        return new Vector2(_rings[index].X * scale, -_rings[index].Y * scale);
    }

    /// <summary>
    /// Takes every ring overlapping the player, and reports how many.
    /// </summary>
    /// <param name="feet">The player's position, which is at its feet.</param>
    public int Collect(Vector2 feet)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        float halfWidth = BodyHalfWidth * scale;
        float ringHalf = RingPixels / 2f * scale;

        // The player's origin is at the feet, so the body reaches up from there.
        float left = feet.X - halfWidth - ringHalf;
        float right = feet.X + halfWidth + ringHalf;
        float bottom = feet.Y - BodyBottom * scale - ringHalf;
        float top = feet.Y + BodyTop * scale + ringHalf;

        int taken = 0;
        for (int i = 0; i < _rings.Length; i++)
        {
            if (_taken[i]) continue;
            var at = WorldPosition(i);
            if (at.X < left || at.X > right || at.Y < bottom || at.Y > top) continue;
            _taken[i] = true;
            Collected++;
            taken++;
        }
        return taken;
    }

    /// <summary>Puts every ring back, for a retry.</summary>
    public void Reset()
    {
        Array.Clear(_taken);
        Collected = 0;
    }
}
