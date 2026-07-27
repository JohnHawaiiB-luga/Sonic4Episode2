using System.Numerics;

namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// Turns touch points into the same three inputs a keyboard produces.
/// </summary>
/// <remarks>
/// The original build assumes a pad or a keyboard, so a phone needs somewhere for
/// the input to come from. This lives in the core library rather than in the
/// Android head because the mapping is the part worth testing, and none of it
/// needs a device: feed it screen-space points and read back
/// <see cref="Player.InputX"/>, jump and crouch.
/// <para>
/// The layout is a left-hand steering zone and a right-hand action zone, sized as
/// fractions of the screen so it survives any resolution or aspect. Steering is
/// analogue in intent but reported as -1, 0 or +1, because that is all
/// <see cref="Player"/> takes today.
/// </para>
/// </remarks>
public sealed class VirtualPad
{
    /// <summary>Fraction of the width given over to steering.</summary>
    public const float SteerZoneWidth = 0.4f;

    /// <summary>Fraction of the height the control zones occupy, measured up from the bottom.</summary>
    public const float ZoneHeight = 0.5f;

    /// <summary>
    /// Dead zone around the steering centre, as a fraction of the steer zone's
    /// half-width. Without one a thumb resting mid-zone jitters between
    /// directions.
    /// </summary>
    public const float DeadZone = 0.15f;

    private readonly float _width;
    private readonly float _height;

    public VirtualPad(float screenWidth, float screenHeight)
    {
        _width = screenWidth;
        _height = screenHeight;
    }

    /// <summary>-1, 0 or +1.</summary>
    public float SteerX { get; private set; }

    /// <summary>Whether the jump area is being touched.</summary>
    public bool Jump { get; private set; }

    /// <summary>Whether the crouch area is being touched.</summary>
    public bool Crouch { get; private set; }

    /// <summary>Recomputes the inputs from this frame's touch points.</summary>
    public void Update(IReadOnlyList<Vector2> touches)
    {
        SteerX = 0f;
        Jump = false;
        Crouch = false;

        float active = _height * (1f - ZoneHeight);
        float steerEdge = _width * SteerZoneWidth;

        foreach (var touch in touches)
        {
            // Anything in the upper part of the screen is not a control - that is
            // where the game is, and where a thumb should not be.
            if (touch.Y < active) continue;

            if (touch.X < steerEdge)
            {
                float centre = steerEdge / 2f;
                float offset = (touch.X - centre) / centre;
                if (MathF.Abs(offset) >= DeadZone) SteerX = MathF.Sign(offset);
            }
            else if (touch.X > _width - steerEdge)
            {
                // The action zone splits vertically: jump above, crouch below, so
                // a spin dash is a thumb resting low and the other tapping.
                if (touch.Y < active + (_height - active) / 2f) Jump = true;
                else Crouch = true;
            }
        }
    }

    /// <summary>Hands this frame's inputs to a player.</summary>
    public void ApplyTo(Player player)
    {
        player.InputX = SteerX;
        player.InputJump = Jump;
        player.InputDown = Crouch;
    }
}
