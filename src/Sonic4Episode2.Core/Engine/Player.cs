using System.Numerics;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// A player that runs, jumps and collides with the stage.
/// </summary>
/// <remarks>
/// <b>These constants are placeholders, not recovered values.</b> They were
/// chosen to feel roughly right at this scale — a cell is 20 world units and the
/// player is one cell wide — and they are not Episode II's numbers. The real
/// figures live in the binary's player code and have not been reverse engineered
/// yet. Anything that depends on Sonic actually feeling like Sonic depends on
/// replacing them.
/// <para>
/// The structure is what matters here: horizontal motion resolved separately
/// from vertical, ground checked from the feet, ceiling from the head, and
/// walls from the mid-line. Resolving both axes at once is what produces the
/// classic bug where running into a wall while falling snags you on it.
/// </para>
/// </remarks>
public sealed class Player : GameObject
{
    public const float Width = 16f;
    public const float Height = 36f;

    // Placeholder tuning; see the class note.
    public const float Acceleration = 0.35f;
    public const float Friction = 0.28f;
    public const float MaxSpeed = 7.0f;
    public const float Gravity = 0.42f;
    public const float JumpVelocity = 9.4f;
    public const float TerminalVelocity = 18f;

    private readonly CollisionMap _collision;

    public Player(CollisionMap collision)
    {
        _collision = collision;
        Name = "player";
        OnUpdate = _ => Think();
        OnMove = _ => Move();
    }

    public Vector2 Velocity;
    public bool OnGround { get; private set; }
    public bool FacingLeft { get; private set; }

    /// <summary>Input for the coming frame, set by the host before stepping.</summary>
    public float InputX { get; set; }
    public bool InputJump { get; set; }

    private bool _jumpHeld;

    private void Think()
    {
        if (InputX != 0f)
        {
            Velocity.X += InputX * Acceleration;
            FacingLeft = InputX < 0f;
        }
        else
        {
            // Friction only bleeds speed toward zero; it must never push the
            // player backwards through it.
            float drop = MathF.Min(MathF.Abs(Velocity.X), Friction);
            Velocity.X -= MathF.Sign(Velocity.X) * drop;
        }

        Velocity.X = Math.Clamp(Velocity.X, -MaxSpeed, MaxSpeed);

        // Edge-triggered so holding jump does not re-fire on landing.
        if (InputJump && !_jumpHeld && OnGround)
            Velocity.Y = JumpVelocity;
        _jumpHeld = InputJump;

        Velocity.Y -= Gravity;
        if (Velocity.Y < -TerminalVelocity) Velocity.Y = -TerminalVelocity;
    }

    private void Move()
    {
        var position = Position;

        // Horizontal first, resolved on its own. Doing both axes together is
        // what makes a player snag on a wall while falling past it.
        position.X += Velocity.X;
        float half = Width / 2f;
        float midY = position.Y + Height / 2f;

        if (Velocity.X > 0f && _collision.IsSolidAt(position.X + half, midY))
        {
            var (cellX, _) = _collision.CellAt(position.X + half, midY);
            position.X = cellX * _collision.CellSize - half - 0.01f;
            Velocity.X = 0f;
        }
        else if (Velocity.X < 0f && _collision.IsSolidAt(position.X - half, midY))
        {
            var (cellX, _) = _collision.CellAt(position.X - half, midY);
            position.X = (cellX + 1) * _collision.CellSize + half + 0.01f;
            Velocity.X = 0f;
        }

        // Vertical second.
        position.Y += Velocity.Y;
        OnGround = false;

        if (Velocity.Y <= 0f)
        {
            float? ground = _collision.GroundHeightAt(position.X, position.Y, maxCells: 2);
            if (ground is not null && position.Y <= ground.Value)
            {
                position.Y = ground.Value;
                Velocity.Y = 0f;
                OnGround = true;
            }
        }
        else if (_collision.IsSolidAt(position.X, position.Y + Height))
        {
            var (_, cellY) = _collision.CellAt(position.X, position.Y + Height);
            position.Y = -(cellY + 1) * _collision.CellSize - Height - 0.01f;
            Velocity.Y = 0f;
        }

        Position = position;
    }

    /// <summary>Drops the player onto the first ground below a starting point.</summary>
    public void PlaceOnGround(float worldX, float searchFromY)
    {
        float? ground = _collision.GroundHeightAt(worldX, searchFromY, maxCells: 512);
        Position = new Vector3(worldX, ground ?? searchFromY, 0f);
        Velocity = Vector2.Zero;
    }
}
