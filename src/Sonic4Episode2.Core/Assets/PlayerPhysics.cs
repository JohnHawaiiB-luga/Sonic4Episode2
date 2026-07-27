namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Episode II's own player tuning, read out of <c>Sonic.exe:0X00710520</c>.
/// </summary>
/// <remarks>
/// Seven rows of 108 bytes, one per playable mode, in the same field order
/// Episode I uses for <c>g_gm_player_parameter[7]</c>. Episode I stores these as
/// FX32 fixed-point integers; Episode II stores the speeds as plain floats, so
/// they can be read directly.
/// <para>
/// The table was found by searching for Episode I's jump impulse — 23130 in FX32
/// is 5.64697265625 as a float — immediately followed by its gravity,
/// 0.166015625. What confirms it is four integers Episode II did not
/// change and that no float search would have surfaced: <c>BreathFrames</c> 1800,
/// <c>InvincibleFrames</c> 180, <c>PoolMax</c> 96 and <c>CoyoteFrames</c> 24, packed
/// as <c>u16</c> pairs where Episode I used four <c>int</c>s. Four of the seven
/// jump impulses match Episode I to the bit; the rest Episode II retuned.
/// </para>
/// <para>
/// <b>Units are game pixels per frame at 60 Hz.</b> A collision cell spans 64 of
/// those pixels and {StageAssembler.CellSize} world units, so
/// <see cref="WorldPerPixel"/> converts. Regenerate with <c>tools/physics.py</c>.
/// </para>
/// </remarks>
/// <param name="GroundAcceleration">Added to ground speed per frame while steering.</param>
/// <param name="TopSpeed">Ground speed cap.</param>
/// <param name="GroundDeceleration">Bled off per frame with no input, and when braking.</param>
/// <param name="JumpImpulse">Upward speed applied on the jump frame.</param>
/// <param name="Gravity">Added to downward speed per airborne frame.</param>
/// <param name="TerminalVelocity">Downward speed cap.</param>
/// <param name="AirAcceleration">Horizontal acceleration while airborne.</param>
/// <param name="AirSpeedMax">Horizontal speed cap while airborne.</param>
/// <param name="AirDrag">Horizontal speed bled off per airborne frame with no input.</param>
/// <param name="CoyoteFrames">Frames after leaving ground before gravity applies.</param>
public readonly record struct PlayerPhysics(
    float GroundAcceleration,
    float TopSpeed,
    float GroundDeceleration,
    float SpinDashBase,
    float SpinDashCharge,
    float SpinDashMax,
    float RollFriction,
    float DownhillTopSpeedBonus,
    float SlopeFactor,
    float SlopeSpeedMax,
    float SlopeFactorRolling,
    float SlopeFactorRollingAlt,
    float SlopeFactorPipe,
    float SlopeFactorPinball,
    float JumpImpulse,
    float Gravity,
    float TerminalVelocity,
    float PushMax,
    float AirAcceleration,
    float AirSpeedMax,
    float AirDrag,
    float PinballAcceleration,
    float PinballTopSpeed,
    float PinballDeceleration,
    float PinballDownhillBonus,
    int BreathFrames,
    int InvincibleFrames,
    int PoolMax,
    int CoyoteFrames)
{
    /// <summary>Game pixels spanned by one collision cell.</summary>
    /// <remarks>
    /// From the collision format: a cell holds 64 height columns, and its 32
    /// height units are two pixels each.
    /// </remarks>
    public const float PixelsPerCell = 64f;

    /// <summary>Frames per second the constants above assume.</summary>
    public const int FrameRate = 60;

    /// <summary>World units per game pixel, for converting the values above.</summary>
    public static float WorldPerPixel => StageAssembler.CellSize / PixelsPerCell;

    /// <summary>Every mode, indexed as Episode I indexes its character ids.</summary>
    public static readonly PlayerPhysics[] All =
    {
        // Sonic
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // SuperSonic
        new(0.106201171875f, 15.0f, 0.5f, 9.0f, 6.0f, 15.0f, 0.03125f, 3.0f, 0.0703125f, 15.0f, 0.234375f, 0.1171875f, 0.3125f, 0.3125f, 7.984130859375f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 1.0f, 0.106201171875f, 61440.0f, 0.503662109375f, 4.5f, 1800, 180, 96, 24),
        // SpecialStage
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.125f, 5.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // Pinball
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // PinballSuper
        new(0.106201171875f, 15.0f, 0.5f, 9.0f, 6.0f, 15.0f, 0.03125f, 2.0f, 0.0703125f, 15.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 7.984130859375f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 1.0f, 0.106201171875f, 61440.0f, 0.503662109375f, 4.5f, 1800, 180, 96, 24),
        // MadGear
        new(0.035400390625f, 10.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 8.0f, 0.125f, 12.5f, 0.125f, 0.125f, 0.125f, 0.125f, 4.705810546875f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.125f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 120),
        // MadGearSuper
        new(0.106201171875f, 11.0f, 0.125f, 9.0f, 6.0f, 15.0f, 0.03125f, 10.0f, 0.125f, 12.5f, 0.125f, 0.125f, 0.125f, 0.125f, 6.6534423828125f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 0.25f, 0.106201171875f, 61440.0f, 0.503662109375f, 4.5f, 1800, 180, 96, 120),
    };

    /// <summary>Ordinary Sonic — the row the stage scene uses.</summary>
    public static PlayerPhysics Sonic => All[0];
}
