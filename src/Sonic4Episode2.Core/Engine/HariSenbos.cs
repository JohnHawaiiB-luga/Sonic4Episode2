using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

public enum HariSenboPhase
{
    Static,
    Waiting,
    Windup,
    Extending,
    Inflated,
}

public readonly record struct HariSenboPlacement(
    int X,
    int Y,
    int ObjectId,
    int Flags,
    byte Width,
    byte Height,
    int Parameter);

public readonly record struct HariSenboBounds(
    int Left,
    int Top,
    int Right,
    int Bottom);

/// <summary>The stationary pufferfish enemies in Episode I acts.</summary>
/// <remarks>
/// <b>VERIFIED.</b> Beta 8's PC initializer at <c>0x0047C1C0</c> assigns
/// attack and defense rectangles of ±12 and ±22 pixels. The red id-15 variant
/// expands its attack rectangle to ±24 pixels.
/// <para>
/// <b>VERIFIED.</b> Raw event width and height select the red variant's wait
/// and inflated durations in units of 30 frames, with a 300-frame fallback.
/// Its windup is 60 frames. The same flow appears in arm64
/// <c>GmEneHariSenboInit</c> at <c>0x0049C780</c>.
/// </para>
/// <para>
/// <b>VERIFIED.</b> Beta 8's <c>EP1_ENE_HARI_MTN.AMB</c> gives the extension
/// action a 0..15 frame span at 60 Hz. Armor becomes active before that action;
/// the larger attack rectangle becomes active when it ends.
/// </para>
/// <para>
/// <b>OPEN.</b> Models, animation blending, jet effects, sound, score, animal
/// release and the player's enemy-defeat reaction are not represented.
/// </para>
/// </remarks>
public sealed class HariSenbos
{
    public const int AttackHalfSizePixels = 12;
    public const int DefenseHalfSizePixels = 22;
    public const int InflatedAttackHalfSizePixels = 24;
    public const int FramesPerParameterUnit = 30;
    public const int DefaultPhaseFrames = 300;
    public const int WindupFrames = 60;
    public const int ExtensionFrames = 15;

    private sealed class State(HariSenboPlacement placement)
    {
        public HariSenboPlacement Placement { get; } = placement;
        public Vector2 Position { get; } = new(
            placement.X * PlayerPhysics.WorldPerPixel,
            -placement.Y * PlayerPhysics.WorldPerPixel);
        public HariSenboPhase Phase =
            placement.ObjectId == 0
                ? HariSenboPhase.Static
                : HariSenboPhase.Waiting;
        public int Timer;
    }

    private readonly State[] _states;

    public HariSenbos(IReadOnlyList<HariSenboPlacement> placements)
    {
        _states = placements
            .Where(placement =>
                ObjectCatalog.Is(placement.ObjectId, "HariSenbo"))
            .Select(placement => new State(placement))
            .ToArray();
    }

    public int Count => _states.Length;

    public HariSenboPlacement PlacementAt(int index) =>
        _states[index].Placement;

    public int ObjectIdAt(int index) =>
        _states[index].Placement.ObjectId;

    public Vector2 PositionOf(int index) =>
        _states[index].Position;

    public HariSenboPhase PhaseAt(int index) =>
        _states[index].Phase;

    public bool IsArmoredAt(int index) =>
        _states[index].Phase is
            HariSenboPhase.Extending or
            HariSenboPhase.Inflated;

    public int WaitFramesAt(int index) =>
        DurationOf(_states[index].Placement.Width);

    public int InflatedFramesAt(int index) =>
        DurationOf(_states[index].Placement.Height);

    public int PhaseFramesRemainingAt(int index)
    {
        State state = _states[index];
        return state.Phase switch
        {
            HariSenboPhase.Waiting =>
                Math.Max(0, WaitFramesAt(index) - state.Timer),
            HariSenboPhase.Windup or HariSenboPhase.Extending =>
                Math.Max(0, state.Timer),
            HariSenboPhase.Inflated =>
                Math.Max(0, InflatedFramesAt(index) - state.Timer),
            _ => 0,
        };
    }

    public HariSenboBounds AttackBoundsAt(int index)
    {
        int halfSize =
            _states[index].Phase == HariSenboPhase.Inflated
                ? InflatedAttackHalfSizePixels
                : AttackHalfSizePixels;
        return SymmetricBounds(halfSize);
    }

    public HariSenboBounds DefenseBoundsAt(int index) =>
        SymmetricBounds(DefenseHalfSizePixels);

    public static HariSenbos FromEventData(ReadOnlySpan<byte> data)
    {
        var (_, _, records) =
            BlockGrid.Walk(data, EventPlacements.RecordStride);
        var placements = new List<HariSenboPlacement>();

        foreach (var (blockX, blockY, at) in records)
        {
            ReadOnlySpan<byte> record =
                data.Slice(at, EventPlacements.RecordStride);
            int objectId =
                BinaryPrimitives.ReadUInt16LittleEndian(record[2..]);
            if (!ObjectCatalog.Is(objectId, "HariSenbo"))
                continue;

            placements.Add(new HariSenboPlacement(
                blockX * EventPlacements.BlockPitch + record[0],
                blockY * EventPlacements.BlockPitch + record[1],
                objectId,
                BinaryPrimitives.ReadUInt16LittleEndian(record[4..]),
                record[8],
                record[9],
                BinaryPrimitives.ReadUInt16LittleEndian(record[10..])));
        }

        return new HariSenbos(placements);
    }

    public static HariSenbos FromActArchive(ReadOnlyMemory<byte> data)
    {
        var archive = AmbArchive.Parse(data);
        foreach (AmbEntry entry in archive.Entries)
        {
            string label = entry.Name.Replace('\\', '/');
            label = label[(label.LastIndexOf('/') + 1)..];
            if (!label.EndsWith(".EV", StringComparison.OrdinalIgnoreCase))
                continue;

            string stem = label[..^3];
            if (stem.Length > 0 && char.IsDigit(stem[^1]))
                return FromEventData(archive.Read(entry).Span);
        }

        return new HariSenbos([]);
    }

    public void Step()
    {
        for (int i = 0; i < _states.Length; i++)
            Advance(i, _states[i]);
    }

    public bool Step(Player player)
    {
        Step();
        return Check(player);
    }

    public bool Check(Player player)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        float playerLeft = player.Position.X - Player.Width / 2f;
        float playerRight = player.Position.X + Player.Width / 2f;
        float playerBottom = player.Position.Y;
        float playerTop = player.Position.Y + Player.Height;

        for (int i = 0; i < _states.Length; i++)
        {
            State state = _states[i];
            HariSenboBounds bounds = AttackBoundsAt(i);
            float enemyLeft =
                state.Position.X + bounds.Left * scale;
            float enemyRight =
                state.Position.X + bounds.Right * scale;
            float enemyBottom =
                state.Position.Y - bounds.Bottom * scale;
            float enemyTop =
                state.Position.Y - bounds.Top * scale;

            if (playerRight >= enemyLeft &&
                playerLeft <= enemyRight &&
                playerTop >= enemyBottom &&
                playerBottom <= enemyTop)
                return true;
        }

        return false;
    }

    private void Advance(int index, State state)
    {
        switch (state.Phase)
        {
            case HariSenboPhase.Static:
                return;

            case HariSenboPhase.Waiting:
                state.Timer++;
                if (state.Timer >= WaitFramesAt(index))
                {
                    state.Phase = HariSenboPhase.Windup;
                    state.Timer = WindupFrames;
                }
                return;

            case HariSenboPhase.Windup:
                state.Timer--;
                if (state.Timer <= 0)
                {
                    state.Phase = HariSenboPhase.Extending;
                    state.Timer = ExtensionFrames;
                }
                return;

            case HariSenboPhase.Extending:
                state.Timer--;
                if (state.Timer <= 0)
                {
                    state.Phase = HariSenboPhase.Inflated;
                    state.Timer = 0;
                }
                return;

            case HariSenboPhase.Inflated:
                state.Timer++;
                if (state.Timer >= InflatedFramesAt(index))
                {
                    state.Phase = HariSenboPhase.Waiting;
                    state.Timer = 0;
                }
                return;
        }
    }

    private static HariSenboBounds SymmetricBounds(int halfSize) =>
        new(-halfSize, -halfSize, halfSize, halfSize);

    private static int DurationOf(byte parameter) =>
        parameter == 0
            ? DefaultPhaseFrames
            : parameter * FramesPerParameterUnit;
}
