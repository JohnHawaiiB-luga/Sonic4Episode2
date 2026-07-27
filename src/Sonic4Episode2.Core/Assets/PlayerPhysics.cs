namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Episode II's own player tuning, read out of <c>Sonic.exe:0X00710520</c>.
/// </summary>
/// <remarks>
/// <b>3 characters of 11 modes</b>, each row 108
/// bytes, in the same field order Episode I uses for
/// <c>g_gm_player_parameter</c>. The character stride is 1188 bytes,
/// read straight off the engine's own indexing — <c>imul ecx, ecx, 0x4a4</c> at
/// <c>0x0046AEA1</c>, on a character id loaded from the player work struct.
/// Episode I stores these as FX32 fixed-point integers; Episode II stores the
/// speeds as plain floats, so they can be read directly.
/// <para>
/// Characters 0 and 1 are physically identical bar one slope field, and both have
/// a Super mode. Character 2 has no Super — its mode 1 repeats its normal values —
/// which is what you would expect of Metal Sonic. Modes 0 to 6 match Episode I's
/// character enumeration in order and in value; modes 7 and 8 are heavily slowed
/// variants with no Episode I counterpart, and 9 and 10 are not identified.
/// </para>
/// <para>
/// The table was found by searching for Episode I's jump impulse — 23130 in FX32
/// is 5.64697265625 as a float — immediately followed by its gravity,
/// 0.166015625. What confirms it is four integers Episode II did not
/// change and that no float search would have surfaced: <c>BreathFrames</c> 1800,
/// <c>InvincibleFrames</c> 180, <c>PoolMax</c> 96 and <c>CoyoteFrames</c> 24, packed
/// as <c>u16</c> pairs where Episode I used four <c>int</c>s. Four of the mode
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
    /// <summary>
    /// Spin-dash launch speed is <c>8 + charge *
    /// 0.5</c>, so a single charge of
    /// 3 launches at
    /// 9.5
    /// and a full one of 10 at
    /// 13.
    /// </summary>
    /// <remarks>
    /// Episode I uses <c>11.75 + charge * 0.125</c>, which spans 12.125 to 13.0 —
    /// the same ceiling, but almost no reward for charging. Episode II widened the
    /// floor instead. Both constants are doubles, read from the launch expression
    /// at <c>0x00513005</c>.
    /// </remarks>
    public const float SpinDashLaunchBase = 8.0f;

    /// <inheritdoc cref="SpinDashLaunchBase"/>
    public const float SpinDashLaunchPerCharge = 0.5f;

    /// <summary>
    /// Fraction of the charge that bleeds away each frame while winding up.
    /// </summary>
    /// <remarks>
    /// The engine decays proportionally — <c>charge -= charge * this</c> — through
    /// the same decrease-toward-zero helper the ground friction uses, at
    /// <c>0x005A8800</c>.
    /// </remarks>
    public const float SpinDashDecayRate = 0.03125f;

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

    /// <summary>Characters in the table.</summary>
    public const int CharacterCount = 3;

    /// <summary>Modes per character.</summary>
    public const int ModeCount = 11;

    /// <summary>Every row, character-major.</summary>
    public static readonly PlayerPhysics[] All =
    {
        // character 0, Normal
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 0, Super
        new(0.106201171875f, 15.0f, 0.5f, 9.0f, 6.0f, 15.0f, 0.03125f, 3.0f, 0.0703125f, 15.0f, 0.234375f, 0.1171875f, 0.3125f, 0.3125f, 7.984130859375f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 1.0f, 0.106201171875f, 61440.0f, 0.503662109375f, 4.5f, 1800, 180, 96, 24),
        // character 0, SpecialStage
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.125f, 5.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 0, Pinball
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 0, PinballSuper
        new(0.106201171875f, 15.0f, 0.5f, 9.0f, 6.0f, 15.0f, 0.03125f, 2.0f, 0.0703125f, 15.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 7.984130859375f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 1.0f, 0.106201171875f, 61440.0f, 0.503662109375f, 4.5f, 1800, 180, 96, 24),
        // character 0, MadGear
        new(0.035400390625f, 10.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 8.0f, 0.125f, 12.5f, 0.125f, 0.125f, 0.125f, 0.125f, 4.705810546875f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.125f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 120),
        // character 0, MadGearSuper
        new(0.106201171875f, 11.0f, 0.125f, 9.0f, 6.0f, 15.0f, 0.03125f, 10.0f, 0.125f, 12.5f, 0.125f, 0.125f, 0.125f, 0.125f, 6.6534423828125f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 0.25f, 0.106201171875f, 61440.0f, 0.503662109375f, 4.5f, 1800, 180, 96, 120),
        // character 0, Slowed1
        new(0.00177001953125f, 0.22500000894069672f, 0.012500000186264515f, 0.15000000596046448f, 0.10000000149011612f, 0.25f, 0.0031250000465661287f, 0.10000000149011612f, 0.0031250000465661287f, 0.32500001788139343f, 0.0078125f, 0.00390625f, 0.015625f, 0.015625f, 2.823486328125f, 0.166015625f, 15.0f, 1.75f, 0.0031250000465661287f, 0.45000001788139343f, 0.0062500000931322575f, 0.00177001953125f, 0.22500000894069672f, 0.0031250000465661287f, 0.07500000298023224f, 1800, 180, 96, 24),
        // character 0, Slowed2
        new(0.00531005859375f, 0.375f, 0.05000000074505806f, 0.45000001788139343f, 0.30000001192092896f, 0.375f, 0.0031250000465661287f, 0.15000000596046448f, 0.003515625139698386f, 0.375f, 0.01171875f, 0.005859375f, 0.015625f, 0.015625f, 3.9920654296875f, 0.166015625f, 15.0f, 1.75f, 0.00937500037252903f, 0.75f, 0.10000000149011612f, 0.00531005859375f, 1536.0f, 0.05036621168255806f, 0.11250000447034836f, 1800, 180, 96, 24),
        // character 0, Spare1
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 0, Spare2
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 1, Normal
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 1, Super
        new(0.106201171875f, 15.0f, 0.5f, 9.0f, 6.0f, 15.0f, 0.03125f, 3.0f, 0.0703125f, 15.0f, 0.234375f, 0.1171875f, 0.3125f, 0.3125f, 7.984130859375f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 1.0f, 0.106201171875f, 61440.0f, 0.503662109375f, 4.5f, 1800, 180, 96, 24),
        // character 1, SpecialStage
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.125f, 5.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 1, Pinball
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 1, PinballSuper
        new(0.106201171875f, 15.0f, 0.5f, 9.0f, 6.0f, 15.0f, 0.03125f, 2.0f, 0.0703125f, 15.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 7.984130859375f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 1.0f, 0.106201171875f, 61440.0f, 0.503662109375f, 4.5f, 1800, 180, 96, 24),
        // character 1, MadGear
        new(0.035400390625f, 10.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 8.0f, 0.125f, 12.5f, 0.125f, 0.125f, 0.125f, 0.125f, 4.705810546875f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.125f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 120),
        // character 1, MadGearSuper
        new(0.106201171875f, 11.0f, 0.125f, 9.0f, 6.0f, 15.0f, 0.03125f, 10.0f, 0.125f, 12.5f, 0.125f, 0.125f, 0.125f, 0.125f, 6.6534423828125f, 0.166015625f, 15.0f, 1.75f, 0.1875f, 15.0f, 0.25f, 0.106201171875f, 61440.0f, 0.503662109375f, 4.5f, 1800, 180, 96, 120),
        // character 1, Slowed1
        new(0.00177001953125f, 0.22500000894069672f, 0.012500000186264515f, 0.15000000596046448f, 0.10000000149011612f, 0.25f, 0.0031250000465661287f, 0.10000000149011612f, 0.0031250000465661287f, 0.32500001788139343f, 0.0078125f, 0.00390625f, 0.015625f, 0.015625f, 2.823486328125f, 0.166015625f, 15.0f, 1.75f, 0.0031250000465661287f, 0.45000001788139343f, 0.0062500000931322575f, 0.00177001953125f, 0.22500000894069672f, 0.0031250000465661287f, 0.07500000298023224f, 1800, 180, 96, 24),
        // character 1, Slowed2
        new(0.00177001953125f, 0.22500000894069672f, 0.012500000186264515f, 0.15000000596046448f, 0.10000000149011612f, 0.25f, 0.0031250000465661287f, 0.10000000149011612f, 0.0031250000465661287f, 0.32500001788139343f, 0.0078125f, 0.00390625f, 0.015625f, 0.015625f, 2.823486328125f, 0.166015625f, 15.0f, 1.75f, 0.0031250000465661287f, 0.45000001788139343f, 0.0062500000931322575f, 0.00177001953125f, 0.22500000894069672f, 0.0031250000465661287f, 0.07500000298023224f, 1800, 180, 96, 24),
        // character 1, Spare1
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 1, Spare2
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.078125f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, Normal
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, Super
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, SpecialStage
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, Pinball
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, PinballSuper
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, MadGear
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, MadGearSuper
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, Slowed1
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, Slowed2
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, Spare1
        new(0.035400390625f, 9.0f, 0.25f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.046875f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.5f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
        // character 2, Spare2
        new(0.035400390625f, 9.0f, 0.125f, 3.0f, 2.0f, 10.0f, 0.03125f, 2.0f, 0.0625f, 13.0f, 0.15625f, 0.15625f, 0.3125f, 0.3125f, 5.64697265625f, 0.166015625f, 15.0f, 1.75f, 0.0625f, 9.0f, 0.0625f, 0.035400390625f, 9.0f, 0.03125f, 3.0f, 1800, 180, 96, 24),
    };

    /// <summary>One row.</summary>
    public static PlayerPhysics For(int character, int mode) =>
        All[character * ModeCount + mode];

    /// <summary>Ordinary Sonic — the row the stage scene uses.</summary>
    public static PlayerPhysics Sonic => For(0, 0);
}
