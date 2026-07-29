using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>The four static spike orientations.</summary>
public enum NeedleDirection
{
    Up,
    Left,
    Down,
    Right,
}

/// <summary>The static spike placements of a mounted stage.</summary>
/// <remarks>
/// <b>VERIFIED.</b> Episode II's static initializer is
/// <c>GmGmkNeedleEp2Init</c> at <c>0x00548168</c>. Its solid rectangles are the
/// four records at <c>0x0096123E</c>, and its attack rectangles are at
/// <c>0x00961256</c>. The initializer adds the placement flag's low two bits to
/// the base variant at <c>0x005481F0</c>, producing the four directions in this
/// class.
/// <para>
/// The Episode Metal variants use the separate initializer at
/// <c>0x00547234</c> and the tables at <c>0x009611A6</c> and
/// <c>0x009611BE</c>. Those values are data from Episode II's own binary, not
/// Episode I constants.
/// </para>
/// <para>
/// <b>VERIFIED.</b> The normal main at <c>0x00548928</c> enables the attack
/// rectangle of an upward spike only while the player rides its solid top. The
/// other three orientations leave their attack rectangles active.
/// </para>
/// <para>
/// <b>OPEN.</b> <c>ActNeedle</c> dispatches separately at
/// <c>0x005483A8</c> and is intentionally excluded. Its retracting cycle is a
/// separate behaviour, not an animation mode of these static spikes.
/// </para>
/// </remarks>
public sealed class Needles
{
    private readonly record struct PixelRect(
        int Left, int Top, int Right, int Bottom);

    private readonly record struct PixelSolid(
        int Width, int Height, int OffsetX, int OffsetY);

    private static readonly PixelRect[] EpisodeMetalAttack =
    [
        new(-8, -33, 15, -8),
        new(-37, -8, -8, 4),
        new(-12, 32, 12, 8),
        new(8, -6, 37, 4),
    ];

    private static readonly PixelRect[] EpisodeTwoAttack =
    [
        new(-15, -33, 15, -8),
        new(-37, -8, -8, 4),
        new(-12, 32, 12, 8),
        new(8, -6, 37, 4),
    ];

    private static readonly PixelSolid[] EpisodeMetalSolid =
    [
        new(24, 30, -8, -32),
        new(40, 30, -36, -16),
        new(32, 32, -16, 0),
        new(40, 28, -4, -16),
    ];

    private static readonly PixelSolid[] EpisodeTwoSolid =
    [
        new(32, 30, -16, -32),
        new(40, 30, -36, -16),
        new(32, 32, -16, 0),
        new(40, 28, -4, -16),
    ];

    private readonly Placement[] _placements;
    private readonly NeedleDirection[] _directions;
    private readonly Vector2[] _positions;

    public Needles(IReadOnlyList<Placement> placements)
    {
        _placements = placements
            .Where(p => ObjectCatalog.Is(p.ObjectId, "Needle"))
            .ToArray();
        _directions = _placements.Select(DirectionOf).ToArray();
        _positions = _placements
            .Select(p => new Vector2(p.X * PlayerPhysics.WorldPerPixel,
                                     -p.Y * PlayerPhysics.WorldPerPixel))
            .ToArray();
    }

    public int Count => _placements.Length;

    public NeedleDirection DirectionAt(int index) => _directions[index];
    public Vector2 PositionOf(int index) => _positions[index];

    public bool Check(Player player)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        float halfPlayer = Player.Width / 2f;
        bool touchesAttack = false;

        for (int i = 0; i < _positions.Length; i++)
        {
            Vector2 at = _positions[i];
            bool episodeTwo = _placements[i].ObjectId >= 445;
            int direction = (int)_directions[i];
            PixelSolid solid = (episodeTwo ? EpisodeTwoSolid : EpisodeMetalSolid)[direction];
            float solidLeft = at.X + solid.OffsetX * scale;
            float solidRight = solidLeft + solid.Width * scale;
            float solidY1 = at.Y - solid.OffsetY * scale;
            float solidY2 = at.Y - (solid.OffsetY + solid.Height) * scale;
            float solidBottom = MathF.Min(solidY1, solidY2);
            float solidTop = MathF.Max(solidY1, solidY2);
            float playerLeft = player.Position.X - halfPlayer;
            float playerRight = player.Position.X + halfPlayer;
            float playerBottom = player.Position.Y;
            float playerTop = player.Position.Y + Player.Height;
            bool overlapsWidth =
                playerRight >= solidLeft && playerLeft <= solidRight;
            bool overlapsHeight =
                playerTop >= solidBottom && playerBottom <= solidTop;

            if (overlapsHeight &&
                player.Velocity.X > 0f &&
                playerRight >= solidLeft &&
                playerRight - player.Velocity.X <= solidLeft)
            {
                player.Position.X = solidLeft - halfPlayer;
                player.Velocity.X = 0f;
            }
            else if (overlapsHeight &&
                     player.Velocity.X < 0f &&
                     playerLeft <= solidRight &&
                     playerLeft - player.Velocity.X >= solidRight)
            {
                player.Position.X = solidRight + halfPlayer;
                player.Velocity.X = 0f;
            }

            if (overlapsWidth &&
                player.Velocity.Y <= 0f &&
                playerBottom <= solidTop &&
                playerBottom - player.Velocity.Y >= solidTop)
            {
                player.Position.Y = solidTop;
                player.Velocity.Y = 0f;
                player.OnGround = true;
            }
            else if (overlapsWidth &&
                     player.Velocity.Y > 0f &&
                     playerTop >= solidBottom &&
                     playerTop - player.Velocity.Y <= solidBottom)
            {
                player.Position.Y = solidBottom - Player.Height;
                player.Velocity.Y = 0f;
            }

            bool riding = _directions[i] == NeedleDirection.Up &&
                          overlapsWidth &&
                          MathF.Abs(player.Position.Y - solidTop) <= 0.001f &&
                          player.Velocity.Y <= 0f;

            bool attackEnabled = _directions[i] != NeedleDirection.Up || riding;
            if (attackEnabled)
            {
                PixelRect attack =
                    (episodeTwo ? EpisodeTwoAttack : EpisodeMetalAttack)[direction];
                float attackLeft = at.X + attack.Left * scale;
                float attackRight = at.X + attack.Right * scale;
                float y1 = at.Y - attack.Top * scale;
                float y2 = at.Y - attack.Bottom * scale;
                float attackBottom = MathF.Min(y1, y2);
                float attackTop = MathF.Max(y1, y2);

                touchesAttack |=
                    player.Position.X + halfPlayer >= attackLeft &&
                    player.Position.X - halfPlayer <= attackRight &&
                    player.Position.Y + Player.Height >= attackBottom &&
                    player.Position.Y <= attackTop;
            }
        }

        return touchesAttack;
    }

    private static NeedleDirection DirectionOf(Placement placement)
    {
        int direction = placement.ObjectId switch
        {
            >= 84 and <= 87 => placement.ObjectId - 84,
            >= 445 and <= 448 =>
                (placement.ObjectId - 445 + (placement.Flags & 3)) & 3,
            _ => throw new InvalidDataException(
                $"Needle class has unsupported id {placement.ObjectId}"),
        };
        return (NeedleDirection)direction;
    }
}
