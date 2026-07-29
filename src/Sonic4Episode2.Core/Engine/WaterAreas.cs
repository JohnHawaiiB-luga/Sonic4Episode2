using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

public enum WaterAreaDirection
{
    Immediate,
    LeftToRight,
    RightToLeft,
    AboveToBelow,
    BelowToAbove,
}

public readonly record struct WaterAreaPlacement(
    int X,
    int Y,
    int ObjectId,
    int Flags,
    sbyte Left,
    sbyte Top,
    byte Width,
    byte Height,
    int Parameter);

public readonly record struct WaterAreaBounds(
    int Left,
    int Top,
    int Right,
    int Bottom);

/// <summary>Directional regions that change the stage's water surface.</summary>
/// <remarks>
/// <b>VERIFIED.</b> Episode II's <c>GmGmkWaterAreaInit</c> begins at arm64
/// <c>0x0057DB54</c>. It computes the target level as signed
/// <c>left * 100 + top</c>, derives seconds from weighted flag bits 0-9 and
/// multiplies by 60 before calling
/// <c>GmWaterSurfaceRequestChangeWaterLevel</c>.
/// <para>
/// <b>VERIFIED.</b> Ids 123-127 and 492-496 select left-to-right,
/// right-to-left, above-to-below, below-to-above and immediate behavior.
/// Directional rectangles have a 34-pixel minimum on each axis.
/// </para>
/// <para>
/// <b>VERIFIED.</b> The surface task at arm64 <c>0x005EC738</c> advances the
/// current level by <c>(target - current) / (duration - elapsed)</c> and snaps
/// to the target when the remaining difference is below one pixel.
/// </para>
/// <para>
/// <b>VERIFIED.</b> The player water check at <c>0x005A23C4</c> treats the
/// player as submerged when its native Y plus 10 reaches the current surface.
/// It applies the jump and gravity multipliers exposed by <see cref="Player"/>.
/// </para>
/// <para>
/// <b>OPEN.</b> Water rendering, entry effects, bubbles, breath countdown and
/// drowning are not represented.
/// </para>
/// </remarks>
public sealed class WaterAreas
{
    public const int MinimumRegionPixels = 34;
    public const int RestartRangePixels = 128;
    public const int FramesPerSecond = 60;
    public const int ImmersionOffsetPixels = 10;
    public const int NoWaterLevelPixels = ushort.MaxValue;

    private sealed class State(
        WaterAreaPlacement placement,
        WaterAreaDirection direction)
    {
        public WaterAreaPlacement Placement { get; } = placement;
        public WaterAreaDirection Direction { get; } = direction;
        public bool Armed;
    }

    private readonly State[] _states;
    private bool _initialized;
    private float _waterLevelPixels = NoWaterLevelPixels;
    private float _targetWaterLevelPixels = NoWaterLevelPixels;
    private int _transitionFramesRemaining;

    public WaterAreas(IReadOnlyList<WaterAreaPlacement> placements)
    {
        var states = new List<State>();
        foreach (WaterAreaPlacement placement in placements)
        {
            if (!ObjectCatalog.Is(placement.ObjectId, "WaterArea"))
                continue;
            if (!TryDirectionOf(
                    placement.ObjectId,
                    out WaterAreaDirection direction))
                continue;
            states.Add(new State(placement, direction));
        }
        _states = states.ToArray();
    }

    public int Count => _states.Length;
    public float WaterLevelPixels => _waterLevelPixels;
    public float TargetWaterLevelPixels => _targetWaterLevelPixels;
    public int TransitionFramesRemaining => _transitionFramesRemaining;
    public bool HasWater => _waterLevelPixels < NoWaterLevelPixels;

    public WaterAreaPlacement PlacementAt(int index) =>
        _states[index].Placement;

    public WaterAreaDirection DirectionAt(int index) =>
        _states[index].Direction;

    public int TargetLevelPixelsAt(int index)
    {
        WaterAreaPlacement placement = _states[index].Placement;
        return unchecked((ushort)(
            placement.Left * 100 + placement.Top));
    }

    public int TransitionFramesAt(int index) =>
        TransitionSeconds(_states[index].Placement.Flags) *
        FramesPerSecond;

    public WaterAreaBounds BoundsPixelsAt(int index)
    {
        WaterAreaPlacement placement = _states[index].Placement;
        int width = Math.Max((int)placement.Width, MinimumRegionPixels);
        int height = Math.Max((int)placement.Height, MinimumRegionPixels);
        int halfWidth = width / 2;
        int halfHeight = height / 2;
        return new WaterAreaBounds(
            placement.X - halfWidth,
            placement.Y - halfHeight,
            placement.X + halfWidth,
            placement.Y + halfHeight);
    }

    public static WaterAreas FromEventData(ReadOnlySpan<byte> data)
    {
        var (_, _, records) =
            BlockGrid.Walk(data, EventPlacements.RecordStride);
        var placements = new List<WaterAreaPlacement>();

        foreach (var (blockX, blockY, at) in records)
        {
            ReadOnlySpan<byte> record =
                data.Slice(at, EventPlacements.RecordStride);
            int objectId =
                BinaryPrimitives.ReadUInt16LittleEndian(record[2..]);
            if (!ObjectCatalog.Is(objectId, "WaterArea"))
                continue;

            placements.Add(new WaterAreaPlacement(
                blockX * EventPlacements.BlockPitch + record[0],
                blockY * EventPlacements.BlockPitch + record[1],
                objectId,
                BinaryPrimitives.ReadUInt16LittleEndian(record[4..]),
                unchecked((sbyte)record[6]),
                unchecked((sbyte)record[7]),
                record[8],
                record[9],
                BinaryPrimitives.ReadUInt16LittleEndian(record[10..])));
        }

        return new WaterAreas(placements);
    }

    public static WaterAreas FromActArchive(ReadOnlyMemory<byte> data)
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

        return new WaterAreas([]);
    }

    /// <summary>
    /// Applies type-0 areas near the restart position. The 128-pixel comparison
    /// is inclusive on both axes, matching the native initializer.
    /// </summary>
    public int Initialize(Player player)
    {
        if (_initialized)
        {
            ApplyUnderwaterState(player);
            return 0;
        }

        _initialized = true;
        float scale = PlayerPhysics.WorldPerPixel;
        Vector2 restart = new(
            player.Position.X / scale,
            -player.Position.Y / scale);
        int requests = 0;

        for (int i = 0; i < _states.Length; i++)
        {
            State state = _states[i];
            if (state.Direction != WaterAreaDirection.Immediate)
                continue;

            WaterAreaPlacement placement = state.Placement;
            if (MathF.Abs(restart.X - placement.X) > RestartRangePixels ||
                MathF.Abs(restart.Y - placement.Y) > RestartRangePixels)
                continue;

            RequestWaterLevel(
                TargetLevelPixelsAt(i),
                TransitionFramesAt(i));
            requests++;
        }

        ApplyUnderwaterState(player);
        return requests;
    }

    /// <summary>
    /// Advances the water transition and directional regions for one player
    /// frame, returning how many regions requested a new level.
    /// </summary>
    public int Step(Player player)
    {
        if (!_initialized)
            Initialize(player);

        AdvanceWaterLevel();

        float scale = PlayerPhysics.WorldPerPixel;
        float playerX = player.Position.X / scale;
        float playerY = -player.Position.Y / scale;
        float playerLeft = playerX - Player.Width / (2f * scale);
        float playerRight = playerX + Player.Width / (2f * scale);
        float playerTop = playerY - Player.Height / scale;
        float playerBottom = playerY;
        int requests = 0;

        for (int i = 0; i < _states.Length; i++)
        {
            State state = _states[i];
            if (state.Direction == WaterAreaDirection.Immediate)
                continue;

            WaterAreaBounds bounds = BoundsPixelsAt(i);
            bool inside =
                playerRight >= bounds.Left &&
                playerLeft <= bounds.Right &&
                playerBottom >= bounds.Top &&
                playerTop <= bounds.Bottom;
            bool sourceSide = IsSourceSide(
                state.Direction,
                playerX,
                playerY,
                state.Placement);

            if (!state.Armed)
            {
                if (inside && sourceSide)
                    state.Armed = true;
                continue;
            }

            if (inside)
                continue;
            state.Armed = false;
            if (sourceSide)
                continue;

            int frames = TransitionFramesAt(i);
            RequestWaterLevel(TargetLevelPixelsAt(i), frames);
            requests++;
        }

        ApplyUnderwaterState(player);
        return requests;
    }

    private void RequestWaterLevel(int levelPixels, int frames)
    {
        _targetWaterLevelPixels = levelPixels;
        _transitionFramesRemaining = Math.Max(0, frames);
        if (_transitionFramesRemaining == 0)
            _waterLevelPixels = _targetWaterLevelPixels;
    }

    private void AdvanceWaterLevel()
    {
        if (_transitionFramesRemaining <= 0)
            return;

        _waterLevelPixels +=
            (_targetWaterLevelPixels - _waterLevelPixels) /
            _transitionFramesRemaining;
        _transitionFramesRemaining--;

        if (_transitionFramesRemaining == 0 ||
            MathF.Abs(
                _targetWaterLevelPixels - _waterLevelPixels) < 1f)
        {
            _waterLevelPixels = _targetWaterLevelPixels;
            _transitionFramesRemaining = 0;
        }
    }

    private void ApplyUnderwaterState(Player player)
    {
        float scale = PlayerPhysics.WorldPerPixel;
        float nativeY = -player.Position.Y / scale;
        player.SetUnderwater(
            HasWater &&
            nativeY + ImmersionOffsetPixels >= _waterLevelPixels);
    }

    private static bool IsSourceSide(
        WaterAreaDirection direction,
        float playerX,
        float playerY,
        WaterAreaPlacement placement) =>
        direction switch
        {
            WaterAreaDirection.LeftToRight =>
                playerX < placement.X,
            WaterAreaDirection.RightToLeft =>
                placement.X < playerX,
            WaterAreaDirection.AboveToBelow =>
                playerY < placement.Y,
            WaterAreaDirection.BelowToAbove =>
                placement.Y < playerY,
            _ => false,
        };

    private static bool TryDirectionOf(
        int objectId,
        out WaterAreaDirection direction)
    {
        direction = objectId switch
        {
            123 or 492 => WaterAreaDirection.LeftToRight,
            124 or 493 => WaterAreaDirection.RightToLeft,
            125 or 494 => WaterAreaDirection.AboveToBelow,
            126 or 495 => WaterAreaDirection.BelowToAbove,
            127 or 496 => WaterAreaDirection.Immediate,
            _ => default,
        };
        return objectId is >= 123 and <= 127 or >= 492 and <= 496;
    }

    private static int TransitionSeconds(int flags)
    {
        int seconds = 0;
        for (int bit = 0; bit < 10; bit++)
        {
            if ((flags & (1 << bit)) != 0)
                seconds += bit + 1;
        }
        return seconds;
    }
}
