using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

public readonly record struct BumperHitbox(
    int Left,
    int Top,
    int Right,
    int Bottom);

public readonly record struct BumperImpact(
    Vector2 Velocity,
    int ControlLockFrames);

/// <summary>The metal bumpers in Episode Metal stages.</summary>
/// <remarks>
/// <b>VERIFIED.</b> Episode II's <c>GmGmkBumperInit</c> at arm64
/// <c>0x00523FCC</c> indexes the hitbox table at <c>0x0095F4B8</c> and the
/// A16 angle table at <c>0x0095F4A4</c> by object id minus 150.
/// <para>
/// <b>VERIFIED.</b> Its collision callback at <c>0x00524278</c> clips launch
/// velocity to ±4 horizontal and ±6 vertical game pixels per frame, and passes
/// either 5 or 15 control-lock frames to <c>GmPlySeqInitPinballAir</c>.
/// </para>
/// <para>
/// <b>OPEN.</b> Hit animation, particles, sound, vibration and the event flag
/// that disables homing recovery are not represented.
/// </para>
/// </remarks>
public sealed class Bumpers
{
    private const int FirstObjectId = 150;
    private const float HorizontalLimitPixels = 4f;
    private const float VerticalLimitPixels = 6f;
    private const float DiagonalPixels = 5f;
    private const float DeflectionPixels = 3f;
    private const float DirectionThresholdPixels = 8f;

    private static readonly int[] Angles =
        [32768, 0, 16384, 49152, 16384, 0, 32768, 49152, 0, 16384];

    private static readonly BumperHitbox[] Hitboxes =
    [
        new(-48, 0, 48, 28),
        new(-48, -28, 48, 0),
        new(0, -48, 28, 48),
        new(-28, -48, 0, 48),
        new(0, 0, 64, 64),
        new(0, -64, 64, 0),
        new(-64, 0, 0, 64),
        new(-64, -64, 0, 0),
        new(-24, -8, 24, 8),
        new(-8, -24, 8, 24),
    ];

    private sealed class State(Placement placement, int variant)
    {
        public int ObjectId { get; } = placement.ObjectId;
        public int Variant { get; } = variant;
        public Vector2 Position { get; } = new(
            placement.X * PlayerPhysics.WorldPerPixel,
            -placement.Y * PlayerPhysics.WorldPerPixel);
        public bool Inside;
    }

    private readonly State[] _states;

    public Bumpers(IReadOnlyList<Placement> placements)
    {
        _states = placements
            .Where(p => ObjectCatalog.Is(p.ObjectId, "Bumper"))
            .Select(p => new { Placement = p, Variant = p.ObjectId - FirstObjectId })
            .Where(p => (uint)p.Variant < (uint)Hitboxes.Length)
            .Select(p => new State(p.Placement, p.Variant))
            .ToArray();
    }

    public int Count => _states.Length;
    public int ObjectIdAt(int index) => _states[index].ObjectId;
    public Vector2 PositionOf(int index) => _states[index].Position;
    public BumperHitbox HitboxAt(int index) => Hitboxes[_states[index].Variant];
    public int AngleA16At(int index) => Angles[_states[index].Variant];

    /// <summary>
    /// Checks a player-sized target by its center and returns the first newly
    /// entered bumper's launch, if any.
    /// </summary>
    public BumperImpact? Check(Vector2 center, Vector2 velocity) =>
        Check(
            center,
            velocity,
            Player.Width / 2f,
            Player.Height / 2f);

    /// <summary>Checks and launches a live player touching a bumper.</summary>
    public bool Check(Player player)
    {
        if (player.IsDead)
            return false;

        Vector2 center = new(
            player.Position.X,
            player.Position.Y + Player.Height / 2f);
        BumperImpact? impact = Check(
            center,
            player.Velocity,
            Player.Width / 2f,
            Player.Height / 2f);
        if (impact is null)
            return false;

        player.LaunchFromBumper(
            impact.Value.Velocity,
            impact.Value.ControlLockFrames);
        return true;
    }

    private BumperImpact? Check(
        Vector2 center,
        Vector2 velocity,
        float halfWidth,
        float halfHeight)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        float halfWidthPixels = halfWidth / scale;
        float halfHeightPixels = halfHeight / scale;
        BumperImpact? impact = null;

        foreach (State state in _states)
        {
            Vector2 relative = new(
                (center.X - state.Position.X) / scale,
                -(center.Y - state.Position.Y) / scale);
            BumperHitbox hitbox = Hitboxes[state.Variant];
            bool overlaps =
                relative.X + halfWidthPixels >= hitbox.Left &&
                relative.X - halfWidthPixels <= hitbox.Right &&
                relative.Y + halfHeightPixels >= hitbox.Top &&
                relative.Y - halfHeightPixels <= hitbox.Bottom;
            bool inside =
                overlaps && AcceptsShape(state.Variant, hitbox, relative);

            if (inside && !state.Inside && impact is null)
            {
                Vector2 launchRelative =
                    new(relative.X, relative.Y - 3f);
                impact = Launch(
                    state.Variant,
                    hitbox,
                    launchRelative,
                    velocity);
            }
            state.Inside = inside;
        }

        return impact;
    }

    private static bool AcceptsShape(
        int variant,
        BumperHitbox hitbox,
        Vector2 point)
    {
        float left = hitbox.Left;
        float top = hitbox.Top;
        float right = hitbox.Right;
        float bottom = hitbox.Bottom;

        return variant switch
        {
            0 => IsRightOf(new(right, bottom * 0.5f), new(0f, bottom), point) &&
                 IsRightOf(new(0f, bottom), new(left, bottom * 0.5f), point),
            1 => IsRightOf(new(left, top * 0.4f), new(0f, top), point) &&
                 IsRightOf(new(0f, top), new(right, top * 0.4f), point),
            2 => IsRightOf(new(right * 0.4f, top), new(right, 0f), point) &&
                 IsRightOf(new(right, 0f), new(right * 0.4f, bottom), point),
            3 => IsRightOf(new(left * 0.4f, bottom), new(left, 0f), point) &&
                 IsRightOf(new(left, 0f), new(left * 0.4f, top), point),
            4 => IsRightOf(new(right, bottom * 0.2f),
                           new(right * 0.2f, bottom), point),
            5 => IsRightOf(new(right * 0.2f, top),
                           new(right, top * 0.2f), point),
            6 => IsRightOf(new(left * 0.2f, bottom),
                           new(left, bottom * 0.2f), point),
            7 => IsRightOf(new(left, top * 0.2f),
                           new(right * 0.2f, top), point),
            _ => true,
        };
    }

    private static bool IsRightOf(
        Vector2 start,
        Vector2 end,
        Vector2 point)
    {
        Vector2 line = end - start;
        Vector2 offset = point - start;
        return line.X * offset.Y - line.Y * offset.X > 0f;
    }

    private static BumperImpact Launch(
        int variant,
        BumperHitbox hitbox,
        Vector2 relative,
        Vector2 worldVelocity)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        float x = worldVelocity.X / scale;
        float y = -worldVelocity.Y / scale;
        int lockFrames;

        switch (variant)
        {
            case 0:
                if (relative.X > hitbox.Right + DirectionThresholdPixels)
                {
                    x = HorizontalLimitPixels;
                    lockFrames = 15;
                }
                else if (relative.X < hitbox.Left - DirectionThresholdPixels)
                {
                    x = -HorizontalLimitPixels;
                    lockFrames = 15;
                }
                else
                {
                    y = VerticalLimitPixels;
                    DeflectHorizontal(ref x, relative.X);
                    lockFrames = 5;
                }
                break;

            case 1:
                if (relative.X > hitbox.Right)
                {
                    x = HorizontalLimitPixels;
                    lockFrames = 15;
                }
                else if (relative.X < hitbox.Left)
                {
                    x = -HorizontalLimitPixels;
                    lockFrames = 15;
                }
                else
                {
                    y = -VerticalLimitPixels;
                    DeflectHorizontal(ref x, relative.X);
                    lockFrames = 5;
                }
                break;

            case 2:
                if (relative.Y < hitbox.Top)
                {
                    y = -VerticalLimitPixels;
                    lockFrames = 5;
                }
                else if (relative.Y > hitbox.Bottom)
                {
                    y = VerticalLimitPixels;
                    lockFrames = 5;
                }
                else
                {
                    x = HorizontalLimitPixels;
                    DeflectVertical(ref y, relative.Y);
                    lockFrames = 15;
                }
                break;

            case 3:
                if (relative.Y < hitbox.Top)
                {
                    y = -VerticalLimitPixels;
                    lockFrames = 5;
                }
                else if (relative.Y > hitbox.Bottom)
                {
                    y = VerticalLimitPixels;
                    lockFrames = 5;
                }
                else
                {
                    x = -HorizontalLimitPixels;
                    DeflectVertical(ref y, relative.Y);
                    lockFrames = 15;
                }
                break;

            case 4:
                x = DiagonalPixels;
                y = DiagonalPixels;
                lockFrames = 5;
                break;

            case 5:
                x = DiagonalPixels;
                y = -DiagonalPixels;
                lockFrames = 5;
                break;

            case 6:
                x = -DiagonalPixels;
                y = DiagonalPixels;
                lockFrames = 5;
                break;

            case 7:
                x = -DiagonalPixels;
                y = -DiagonalPixels;
                lockFrames = 5;
                break;

            case 8:
                if (relative.X > hitbox.Right)
                {
                    x = HorizontalLimitPixels;
                    lockFrames = 15;
                }
                else if (relative.X < hitbox.Left)
                {
                    x = -HorizontalLimitPixels;
                    lockFrames = 15;
                }
                else
                {
                    y = relative.Y < -DirectionThresholdPixels
                        ? -VerticalLimitPixels
                        : VerticalLimitPixels;
                    DeflectHorizontal(ref x, relative.X);
                    lockFrames = 5;
                }
                break;

            default:
                if (relative.Y < hitbox.Top)
                {
                    y = -VerticalLimitPixels;
                    lockFrames = 5;
                }
                else if (relative.Y > hitbox.Bottom)
                {
                    y = VerticalLimitPixels;
                    lockFrames = 5;
                }
                else if (relative.X < -DirectionThresholdPixels)
                {
                    x = -HorizontalLimitPixels;
                    if (relative.Y < -DirectionThresholdPixels)
                        y -= DeflectionPixels;
                    lockFrames = 15;
                }
                else
                {
                    x = HorizontalLimitPixels;
                    if (relative.Y < -DirectionThresholdPixels)
                        y -= DeflectionPixels;
                    else if (relative.X > DirectionThresholdPixels)
                        y += DeflectionPixels;
                    lockFrames = 15;
                }
                break;
        }

        x = Math.Clamp(x, -HorizontalLimitPixels, HorizontalLimitPixels);
        y = Math.Clamp(y, -VerticalLimitPixels, VerticalLimitPixels);
        return new BumperImpact(
            new Vector2(x * scale, -y * scale),
            lockFrames);
    }

    private static void DeflectHorizontal(ref float velocity, float offset)
    {
        if (offset < -DirectionThresholdPixels)
            velocity -= DeflectionPixels;
        else if (offset > DirectionThresholdPixels)
            velocity += DeflectionPixels;
    }

    private static void DeflectVertical(ref float velocity, float offset)
    {
        if (offset < -DirectionThresholdPixels)
            velocity -= DeflectionPixels;
        else if (offset > DirectionThresholdPixels)
            velocity += DeflectionPixels;
    }
}
