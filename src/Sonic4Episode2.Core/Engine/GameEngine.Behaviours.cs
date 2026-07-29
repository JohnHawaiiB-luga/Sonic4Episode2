using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Core.Engine;

public sealed partial class GameEngine
{
    private Needles? _needles;
    private Lands? _lands;
    private Bumpers? _bumpers;
    private WaterAreas? _waterAreas;

    /// <summary>The shared player-damage behaviour.</summary>
    public Damage DamageBehaviour { get; } = new();

    /// <summary>The mounted stage's static spikes.</summary>
    public Needles? Needles => Stage is null ? null : _needles;

    /// <summary>The mounted stage's moving platforms.</summary>
    public Lands? Lands => Stage is null ? null : _lands;

    /// <summary>The mounted stage's metal bumpers.</summary>
    public Bumpers? Bumpers => Stage is null ? null : _bumpers;

    /// <summary>The mounted stage's water-level regions.</summary>
    public WaterAreas? WaterAreas => Stage is null ? null : _waterAreas;

    /// <summary>Applies the normal damage transition to the active player.</summary>
    public DamageResult DamagePlayer()
    {
        if (Player is null)
            return new DamageResult(DamageOutcome.Ignored, RingCount);

        DamageResult result = DamageBehaviour.Apply(Player, RingCount);
        RingCount = result.RingsRemaining;
        return result;
    }

    private void MountBehaviours(IReadOnlyList<Placement> placements)
    {
        _needles = new Needles(placements);
        _lands = Sonic4Episode2.Core.Engine.Lands.FromActArchive(
            _content.Read(ActArchive),
            ActArchive);
        _bumpers = new Bumpers(placements);
        _waterAreas =
            Sonic4Episode2.Core.Engine.WaterAreas.FromActArchive(
                _content.Read(ActArchive));
        if (Player is Player player)
        {
            Lands lands = _lands;
            WaterAreas waterAreas = _waterAreas;
            waterAreas.Initialize(player);
            Action<GameObject>? previousEnter = player.OnEnter;
            player.OnEnter = instance =>
            {
                previousEnter?.Invoke(instance);
                if (ReferenceEquals(Player, instance))
                    waterAreas.Step(player);
            };
            Action<GameObject>? previousCollision = player.OnCollide;
            player.OnCollide = instance =>
            {
                previousCollision?.Invoke(instance);
                if (ReferenceEquals(Player, instance))
                    lands.Step(Frame, player);
            };
        }
        Scheduler.Create(
            "GM_NEEDLE",
            _ => CheckNeedles(),
            PriorityObject,
            group: SceneGroup);
        Scheduler.Create(
            "GM_BUMPER",
            _ => CheckBumpers(),
            PriorityObject,
            group: SceneGroup);
    }

    private void CheckNeedles()
    {
        if (Player is not null && _needles?.Check(Player) == true)
            DamagePlayer();
    }

    private void CheckBumpers()
    {
        if (Player is not null)
            _bumpers?.Check(Player);
    }
}
