using System.Buffers.Binary;
using System.Numerics;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

public enum LandMotion
{
    Sinusoid,
    Rectangle,
    Route,
}

public readonly record struct LandPlacement(
    int X,
    int Y,
    int ObjectId,
    int Flags,
    sbyte Left,
    sbyte Top,
    byte Width,
    byte Height,
    int Parameter);

public readonly record struct LandCollisionBox(
    int Width,
    int Height,
    int OffsetX,
    int OffsetY,
    bool OneWay);

/// <summary>The moving platforms in a mounted stage.</summary>
/// <remarks>
/// <b>VERIFIED.</b> Episode II's <c>GmGmkLandInit</c> is at
/// <c>0x0053D53C</c> and its main is at <c>0x0053E6D4</c>. The initializer
/// reads the event record's signed left/top and unsigned width/height fields;
/// these are therefore preserved here instead of using the reduced public
/// <see cref="Placement"/> record.
/// <para>
/// The speed-selector table at <c>0x00960DE8</c> is 4, 2, 3 and 5. Normal
/// platforms use a 1024-step sinusoid, id 98 and id 537 traverse the doubled
/// event rectangle, and id 540 follows points supplied by the
/// <c>LandRoutePos</c> catalog class.
/// </para>
/// <para>
/// Falling starts after 30 ridden frames. The generic enemy constructor loads
/// 0.1640625 px/frame² and 15 px/frame from <c>0x0094E270</c> into the fall
/// fields; Land replaces the terminal field with 7.5 at <c>0x0053EBD4</c>.
/// </para>
/// <para>
/// <b>VERIFIED.</b> Collision dimensions branch on Episode II's 36-entry
/// <c>g_gm_gamedat_zone_type_tbl</c> at <c>0x009571B4</c>. The type-0,
/// type-1, type-2 and type-3 branches begin at <c>0x0053DA84</c>,
/// <c>0x0053DA9C</c>, <c>0x0053DAE8</c> and <c>0x0053DB40</c>.
/// <b>INFERRED.</b> Archives not present in Episode II's active stage-path
/// table inherit the zone type of their directory's listed Episode Metal act.
/// </para>
/// <para>
/// <b>OPEN.</b> Type-3 platform tilt, render pieces, effects and sound are not
/// represented. Their recovered translational movement and Episode II
/// one-way collision are represented.
/// </para>
/// </remarks>
public sealed class Lands
{
    private const float FallAccelerationPixels = 0.1640625f;
    private const float FallTerminalPixels = 7.5f;

    private static readonly int[] Speeds = [4, 2, 3, 5];

    private sealed class State(LandPlacement placement, LandMotion motion)
    {
        public LandPlacement Placement { get; } = placement;
        public LandMotion Motion { get; } = motion;
        public Vector2 PositionPixels = new(placement.X, placement.Y);
        public int PhaseSeed;
        public int RectangleSeed;
        public int LocalPhase;
        public bool MotionTriggered;
        public bool HasBeenRidden;
        public int RiddenFrames;
        public bool Falling;
        public float FallSpeedPixels;
        public int RoutePoint;
        public bool RouteReverse;
        public bool RouteStopped;
    }

    private readonly State[] _states;
    private readonly Dictionary<int, Vector2[]> _routes;
    private readonly string _actArchive;
    private ulong? _lastFrame;
    private int _rider = -1;
    private Vector2 _riderAnchor;

    public Lands(
        IReadOnlyList<LandPlacement> placements,
        string actArchive = "")
    {
        _actArchive = actArchive.Replace('\\', '/');
        _routes = BuildRoutes(placements);
        _states = placements
            .Where(p => ObjectCatalog.Is(p.ObjectId, "Land"))
            .Select(CreateState)
            .ToArray();
    }

    public int Count => _states.Length;

    public LandPlacement PlacementAt(int index) => _states[index].Placement;
    public LandMotion MotionAt(int index) => _states[index].Motion;
    public bool IsFallingAt(int index) => _states[index].Falling;
    public float FallSpeedPixelsAt(int index) => _states[index].FallSpeedPixels;

    public Vector2 PositionOf(int index)
    {
        Vector2 pixels = _states[index].PositionPixels;
        float scale = PlayerPhysics.WorldPerPixel;
        return new Vector2(pixels.X * scale, -pixels.Y * scale);
    }

    public LandCollisionBox CollisionAt(int index) =>
        CollisionFor(_states[index].Placement);

    public static Lands FromEventData(
        ReadOnlySpan<byte> data,
        string actArchive = "")
    {
        var (_, _, records) =
            BlockGrid.Walk(data, EventPlacements.RecordStride);
        var placements = new List<LandPlacement>();

        foreach (var (blockX, blockY, at) in records)
        {
            ReadOnlySpan<byte> record =
                data.Slice(at, EventPlacements.RecordStride);
            int objectId =
                BinaryPrimitives.ReadUInt16LittleEndian(record[2..]);
            if (!ObjectCatalog.Is(objectId, "Land") &&
                !ObjectCatalog.Is(objectId, "LandRoutePos"))
                continue;

            placements.Add(new LandPlacement(
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

        return new Lands(placements, actArchive);
    }

    public static Lands FromActArchive(
        ReadOnlyMemory<byte> data,
        string actArchive)
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
                return FromEventData(
                    archive.Read(entry).Span,
                    actArchive);
        }

        return new Lands([], actArchive);
    }

    public void Step(ulong frame, Player? player = null)
    {
        bool advanced = _lastFrame != frame;
        if (advanced)
        {
            foreach (State state in _states)
                Advance(state, frame);
            _lastFrame = frame;
        }

        int ridden = player is null ? -1 : ResolvePlayer(player);
        if (!advanced)
            return;

        if (ridden >= 0)
        {
            State state = _states[ridden];
            state.HasBeenRidden = true;
            state.MotionTriggered = true;
        }

        foreach (State state in _states)
        {
            if (!state.HasBeenRidden ||
                (state.Placement.Flags & 64) == 0 ||
                state.Falling)
                continue;

            state.RiddenFrames++;
            if (state.RiddenFrames >= 30)
                state.Falling = true;
        }
    }

    private State CreateState(LandPlacement placement)
    {
        LandMotion motion = placement.ObjectId switch
        {
            98 or 537 => LandMotion.Rectangle,
            540 => LandMotion.Route,
            _ => LandMotion.Sinusoid,
        };
        var state = new State(placement, motion);

        if (motion == LandMotion.Sinusoid)
            state.PhaseSeed = InitialSinusoidPhase(placement);
        else if (motion == LandMotion.Rectangle)
            state.RectangleSeed = InitialRectanglePhase(placement);

        return state;
    }

    private void Advance(State state, ulong frame)
    {
        if (state.Falling)
        {
            state.FallSpeedPixels = MathF.Min(
                state.FallSpeedPixels + FallAccelerationPixels,
                FallTerminalPixels);
            state.PositionPixels.Y += state.FallSpeedPixels;
            return;
        }

        switch (state.Motion)
        {
            case LandMotion.Sinusoid:
                AdvanceSinusoid(state, frame);
                break;
            case LandMotion.Rectangle:
                AdvanceRectangle(state, frame);
                break;
            case LandMotion.Route:
                AdvanceRoute(state);
                break;
        }
    }

    private static void AdvanceSinusoid(State state, ulong frame)
    {
        LandPlacement placement = state.Placement;
        if ((placement.Width | placement.Height) == 0)
            return;

        int speed = Speeds[placement.Flags & 3];
        int phase;
        if ((placement.Flags & 4) == 0)
        {
            phase = (int)((frame * (ulong)speed +
                           (ulong)state.PhaseSeed) & 1023);
        }
        else if (!state.MotionTriggered)
        {
            phase = state.PhaseSeed & 1023;
        }
        else
        {
            phase = (state.LocalPhase * speed + state.PhaseSeed) & 1023;
            state.LocalPhase = (state.LocalPhase + 1) & 1023;
        }

        float centerX =
            placement.X + placement.Left + placement.Width * 0.5f;
        float centerY =
            placement.Y + placement.Top + placement.Height * 0.5f;
        float halfWidth = placement.Width >> 1;
        float halfHeight = placement.Height >> 1;
        int xPhase = (placement.Flags & 8) != 0
            ? (phase + 512) & 1023
            : phase;

        state.PositionPixels = new Vector2(
            centerX + halfWidth * Sin(xPhase),
            centerY + halfHeight * Sin(phase));
    }

    private static void AdvanceRectangle(State state, ulong frame)
    {
        LandPlacement placement = state.Placement;
        int left = placement.Left * 2;
        int top = placement.Top * 2;
        int width = placement.Width * 2;
        int height = placement.Height * 2;
        int perimeter = width * 2 + height * 2;
        if (perimeter == 0)
            return;

        int speed = Speeds[placement.Flags & 3];
        int clock =
            (int)((frame * (ulong)speed +
                   (ulong)state.RectangleSeed) & 4095);
        int distance = perimeter * clock / 4096;
        bool reverse = (placement.Flags & 8) != 0;
        int x;
        int y;

        if (!reverse)
        {
            if (distance <= width)
            {
                x = left + distance;
                y = top;
            }
            else if (distance <= width + height)
            {
                x = left + width;
                y = top + distance - width;
            }
            else if (distance <= width * 2 + height)
            {
                x = left + width - (distance - width - height);
                y = top + height;
            }
            else
            {
                x = left;
                y = top + height -
                    (distance - width * 2 - height);
            }
        }
        else
        {
            if (distance <= width)
            {
                x = left + width - distance;
                y = top;
            }
            else if (distance <= width + height)
            {
                x = left;
                y = top + distance - width;
            }
            else if (distance <= width * 2 + height)
            {
                x = left + distance - width - height;
                y = top + height;
            }
            else
            {
                x = left + width;
                y = top + height -
                    (distance - width * 2 - height);
            }
        }

        state.PositionPixels =
            new Vector2(placement.X + x, placement.Y + y);
    }

    private void AdvanceRoute(State state)
    {
        int routeId = state.Placement.Left;
        if (!_routes.TryGetValue(routeId, out Vector2[]? points) ||
            points.Length < 2)
            return;

        if (state.RouteStopped)
        {
            state.PositionPixels = points[^1];
            return;
        }

        int point = Math.Clamp(state.RoutePoint, 0, points.Length - 1);
        Vector2 target = points[point];
        Vector2 delta = target - state.PositionPixels;
        float distance = delta.Length();
        float speed = unchecked((ushort)(short)state.Placement.Top) * 0.5f;

        if (distance > speed)
        {
            if (speed <= 0f)
                return;
            state.PositionPixels += delta / distance * speed;
            return;
        }

        state.PositionPixels = target;
        if (point < points.Length - 1)
        {
            if (point == 0)
                state.RouteReverse = false;
            state.RoutePoint += state.RouteReverse ? -1 : 1;
        }
        else if ((state.Placement.Flags & 1) != 0)
        {
            state.RouteStopped = true;
        }
        else if (point > 0)
        {
            state.RouteReverse = true;
            state.RoutePoint--;
        }
    }

    private int ResolvePlayer(Player player)
    {
        if (_rider >= 0)
        {
            Vector2 platform = PositionOf(_rider);
            Vector2 offset = platform - _riderAnchor;
            Vector2 visual = new(
                player.Position.X + offset.X,
                player.Position.Y + offset.Y);
            Bounds bounds = BoundsAt(_rider);
            bool overlaps =
                visual.X + Player.Width / 2f >= bounds.Left &&
                visual.X - Player.Width / 2f <= bounds.Right;
            bool stays =
                player.Velocity.Y <= 0f &&
                overlaps &&
                visual.Y <= bounds.Top +
                    0.05f * PlayerPhysics.WorldPerPixel &&
                visual.Y - player.Velocity.Y >= bounds.Top -
                    0.05f * PlayerPhysics.WorldPerPixel;

            if (stays)
            {
                player.TempOffset = new Vector3(offset, 0f);
                player.Position.Y = bounds.Top - offset.Y;
                player.Velocity.Y = 0f;
                player.OnGround = true;
                return _rider;
            }

            _rider = -1;
        }

        player.TempOffset = Vector3.Zero;
        for (int i = 0; i < _states.Length; i++)
        {
            Bounds bounds = BoundsAt(i);
            LandCollisionBox collision = CollisionAt(i);
            float halfPlayer = Player.Width / 2f;
            float playerLeft = player.Position.X - halfPlayer;
            float playerRight = player.Position.X + halfPlayer;
            float playerBottom = player.Position.Y;
            float playerTop = player.Position.Y + Player.Height;
            bool overlapsWidth =
                playerRight >= bounds.Left && playerLeft <= bounds.Right;

            if (!collision.OneWay)
            {
                bool overlapsHeight =
                    playerTop >= bounds.Bottom &&
                    playerBottom <= bounds.Top;
                if (overlapsHeight &&
                    player.Velocity.X > 0f &&
                    playerRight >= bounds.Left &&
                    playerRight - player.Velocity.X <= bounds.Left)
                {
                    player.Position.X = bounds.Left - halfPlayer;
                    player.Velocity.X = 0f;
                }
                else if (overlapsHeight &&
                         player.Velocity.X < 0f &&
                         playerLeft <= bounds.Right &&
                         playerLeft - player.Velocity.X >= bounds.Right)
                {
                    player.Position.X = bounds.Right + halfPlayer;
                    player.Velocity.X = 0f;
                }

                if (overlapsWidth &&
                    player.Velocity.Y > 0f &&
                    playerTop >= bounds.Bottom &&
                    playerTop - player.Velocity.Y <= bounds.Bottom)
                {
                    player.Position.Y = bounds.Bottom - Player.Height;
                    player.Velocity.Y = 0f;
                    continue;
                }
            }

            if (!overlapsWidth ||
                player.Velocity.Y > 0f ||
                playerBottom > bounds.Top ||
                playerBottom - player.Velocity.Y < bounds.Top)
                continue;

            player.Position.Y = bounds.Top;
            player.Velocity.Y = 0f;
            player.OnGround = true;
            _rider = i;
            _riderAnchor = PositionOf(i);
            return i;
        }

        return -1;
    }

    private Bounds BoundsAt(int index)
    {
        Vector2 at = PositionOf(index);
        LandCollisionBox collision = CollisionAt(index);
        float scale = PlayerPhysics.WorldPerPixel;
        float left = at.X + collision.OffsetX * scale;
        float right = left + collision.Width * scale;
        float y1 = at.Y - collision.OffsetY * scale;
        float y2 =
            at.Y - (collision.OffsetY + collision.Height) * scale;
        return new Bounds(
            left,
            MathF.Min(y1, y2),
            right,
            MathF.Max(y1, y2));
    }

    private LandCollisionBox CollisionFor(LandPlacement placement)
    {
        int zoneType = ZoneTypeOf(_actArchive);
        bool typeOne = placement.ObjectId is 82 or 535;
        bool typeTwo = placement.ObjectId is 83 or 536;
        bool typeThree = placement.ObjectId is 538 or 539;

        if (typeTwo)
        {
            return zoneType == 8
                ? new LandCollisionBox(24, 32, -12, -15, false)
                : new LandCollisionBox(64, 64, -32, -31, false);
        }

        int width;
        int offsetY;
        if (typeThree)
        {
            (width, offsetY) = zoneType switch
            {
                0 => (56, -17),
                1 => (64, -21),
                2 => (48, -17),
                3 => (56, -17),
                _ => (48, -17),
            };
        }
        else
        {
            bool laterFamily = zoneType >= 5;
            width = typeOne
                ? laterFamily ? 80 : 88
                : laterFamily ? 48 : 56;
            offsetY = laterFamily ? -17 : -21;
        }

        bool oneWay = (placement.Flags & 128) == 0;
        int height = oneWay ? 8 : 24;
        if (oneWay)
            offsetY++;

        return new LandCollisionBox(
            width,
            height,
            -width / 2,
            offsetY,
            oneWay);
    }

    private static int ZoneTypeOf(string actArchive)
    {
        if (actArchive.Contains(
                "G_EP1ZONE4/MAP/CUTSCENE05",
                StringComparison.OrdinalIgnoreCase))
            return 7;
        if (actArchive.Contains(
                "G_EP1ZONE3/MAP/CUTSCENE06",
                StringComparison.OrdinalIgnoreCase))
            return 8;
        if (actArchive.Contains(
                "G_EP1ZONE1/MAP/CUTSCENE07",
                StringComparison.OrdinalIgnoreCase))
            return 9;
        if (actArchive.StartsWith(
                "G_EP1ZONE1/",
                StringComparison.OrdinalIgnoreCase))
            return 0;
        if (actArchive.StartsWith(
                "G_EP1ZONE2/",
                StringComparison.OrdinalIgnoreCase))
            return 3;
        if (actArchive.StartsWith(
                "G_EP1ZONE3/",
                StringComparison.OrdinalIgnoreCase))
            return 4;
        if (actArchive.StartsWith(
                "G_EP1ZONE4/",
                StringComparison.OrdinalIgnoreCase))
            return 6;
        if (actArchive.StartsWith(
                "G_ZONE2/",
                StringComparison.OrdinalIgnoreCase))
            return 1;
        if (actArchive.StartsWith(
                "G_ZONE3/",
                StringComparison.OrdinalIgnoreCase))
            return 2;
        if (actArchive.StartsWith(
                "G_ZONE4/",
                StringComparison.OrdinalIgnoreCase))
            return 3;
        if (actArchive.StartsWith(
                "G_ZONEF/",
                StringComparison.OrdinalIgnoreCase))
            return 4;
        if (actArchive.StartsWith(
                "G_SS/",
                StringComparison.OrdinalIgnoreCase))
            return 5;
        return 0;
    }

    private static int InitialSinusoidPhase(LandPlacement placement)
    {
        if ((placement.Width | placement.Height) == 0)
            return 0;

        int phase;
        if ((placement.Flags & 4) != 0)
        {
            phase = 0;
        }
        else
        {
            bool horizontal = placement.Height < placement.Width;
            int extent = horizontal
                ? placement.Width >> 1
                : placement.Height >> 1;
            float origin = horizontal ? placement.X : placement.Y;
            float center = horizontal
                ? placement.X + placement.Left + placement.Width * 0.5f
                : placement.Y + placement.Top + placement.Height * 0.5f;

            phase = 768;
            while (phase > 256 &&
                   center + extent * Sin(phase) <= origin)
                phase -= 4;
        }

        int offset = ((placement.Flags & 48) >> 4) << 8;
        return (phase - offset) & 16383;
    }

    private static int InitialRectanglePhase(LandPlacement placement)
    {
        int left = placement.Left * 2;
        int top = placement.Top * 2;
        int width = placement.Width * 2;
        int height = placement.Height * 2;
        int perimeter = width * 2 + height * 2;
        if (perimeter == 0)
            return 0;

        if (top == 0)
            return Math.Abs(left) * 4096 / perimeter;
        if (left == 0)
            return (perimeter - Math.Abs(top)) * 4096 / perimeter;
        if (left + width == 0)
            return (width + Math.Abs(top)) * 4096 / perimeter;
        return (perimeter - height - Math.Abs(left)) *
               4096 / perimeter;
    }

    private static Dictionary<int, Vector2[]> BuildRoutes(
        IReadOnlyList<LandPlacement> placements)
    {
        var records = new Dictionary<int, SortedDictionary<int, Vector2>>();
        foreach (LandPlacement placement in placements)
        {
            if (!ObjectCatalog.Is(placement.ObjectId, "LandRoutePos"))
                continue;

            int route = placement.Left;
            int point = placement.Top;
            if (route is < 0 or > 7 || point is < 0 or > 7)
                continue;

            if (!records.TryGetValue(route, out var points))
            {
                points = [];
                records.Add(route, points);
            }
            points[point] = new Vector2(placement.X, placement.Y);
        }

        var routes = new Dictionary<int, Vector2[]>();
        foreach (var (route, points) in records)
        {
            if (points.Count == 0 ||
                points.Keys.Where((point, index) => point != index).Any())
                continue;
            routes[route] = points.Values.ToArray();
        }
        return routes;
    }

    private static float Sin(int phase) =>
        MathF.Sin(2f * MathF.PI * (phase & 1023) / 1024f);

    private readonly record struct Bounds(
        float Left,
        float Bottom,
        float Right,
        float Top);
}
