using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// A player that runs, jumps and collides with the stage.
/// </summary>
/// <remarks>
/// The tuning is <b>Episode II's own</b>, read out of its player parameter table
/// — see <see cref="PlayerPhysics"/>. Those values are in game pixels per frame
/// at 60 Hz, so everything here converts through
/// <see cref="PlayerPhysics.WorldPerPixel"/> and this object moves in world units
/// like everything else.
/// <para>
/// The structure is what matters here: horizontal motion resolved separately
/// from vertical, ground checked from the feet, ceiling from the head, and
/// walls from the mid-line. Resolving both axes at once is what produces the
/// classic bug where running into a wall while falling snags you on it.
/// </para>
/// </remarks>
public sealed class Player : GameObject
{
    /// <summary>Terrain box, from Episode I's player: 12 x 25 game pixels.</summary>
    public static float Width => 12f * PlayerPhysics.WorldPerPixel;
    public static float Height => 25f * PlayerPhysics.WorldPerPixel;

    private readonly CollisionMap _collision;
    private readonly PlayerPhysics _physics;
    private readonly float _scale;

    public Player(CollisionMap collision) : this(collision, PlayerPhysics.Sonic) { }

    public Player(CollisionMap collision, PlayerPhysics physics)
    {
        _collision = collision;
        _physics = physics;
        _scale = PlayerPhysics.WorldPerPixel;
        Name = "player";
        OnUpdate = _ => Think();
        OnMove = _ => Move();
    }

    /// <summary>The tuning this player runs on.</summary>
    public PlayerPhysics Physics => _physics;

    // Every constant in the table is a game-pixel figure; these are the world-unit
    // equivalents this object actually integrates with.
    public float Acceleration => _physics.GroundAcceleration * _scale;
    public float Friction => _physics.GroundDeceleration * _scale;
    public float MaxSpeed => _physics.TopSpeed * _scale;
    public float Gravity => _physics.Gravity * _scale;
    public float JumpVelocity => _physics.JumpImpulse * _scale;
    public float TerminalVelocity => _physics.TerminalVelocity * _scale;
    public float AirAcceleration => _physics.AirAcceleration * _scale;
    public float AirSpeedMax => _physics.AirSpeedMax * _scale;
    public float AirDrag => _physics.AirDrag * _scale;

    public Vector2 Velocity;
    public bool OnGround { get; private set; }
    public bool FacingLeft { get; private set; }

    /// <summary>Input for the coming frame, set by the host before stepping.</summary>
    public float InputX { get; set; }
    public bool InputJump { get; set; }

    private bool _jumpHeld;
    private bool _cuttingJump;

    private void Think()
    {
        // Ground and air are separately tuned in the table, and the difference is
        // large - on the ground Sonic accelerates at 0.0354 px/frame and in the
        // air at 0.0625, with a much weaker brake.
        float accel = OnGround ? Acceleration : AirAcceleration;
        float drag = OnGround ? Friction : AirDrag;
        float cap = OnGround ? MaxSpeed : AirSpeedMax;

        if (InputX != 0f)
        {
            Velocity.X += InputX * accel;
            FacingLeft = InputX < 0f;
        }
        else
        {
            // Drag only bleeds speed toward zero; it must never push the player
            // backwards through it.
            float drop = MathF.Min(MathF.Abs(Velocity.X), drag);
            Velocity.X -= MathF.Sign(Velocity.X) * drop;
        }

        Velocity.X = Math.Clamp(Velocity.X, -cap, cap);

        // Edge-triggered so holding jump does not re-fire on landing.
        if (InputJump && !_jumpHeld && OnGround)
        {
            Velocity.Y = JumpVelocity;
            _cuttingJump = false;
        }
        _jumpHeld = InputJump;

        // Episode II does not clamp the rise on release. It sets a flag when the
        // button comes up while still rising faster than 4 px/frame, and that flag
        // applies gravity a second time each frame until the rise ends.
        if (!InputJump && Velocity.Y > JumpCutThreshold) _cuttingJump = true;
        if (Velocity.Y <= 0f) _cuttingJump = false;

        Velocity.Y -= Gravity;
        if (_cuttingJump) Velocity.Y -= Gravity;
        if (Velocity.Y < -TerminalVelocity) Velocity.Y = -TerminalVelocity;
    }

    /// <summary>
    /// Rise speed above which releasing jump cuts it short — 4 game pixels per
    /// frame, which is <c>16384</c> in Episode I's fixed point.
    /// </summary>
    private float JumpCutThreshold => 4f * _scale;

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
