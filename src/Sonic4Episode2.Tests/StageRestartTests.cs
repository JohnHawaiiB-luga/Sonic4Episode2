using System.Buffers.Binary;
using System.Numerics;
using System.Text;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Core.Engine;

namespace Sonic4Episode2.Tests;

public class StageRestartTests
{
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ClearingTheActPreventsSubsequentDamageFromLeavingADeadStage(bool collectRing)
    {
        var (engine, _) = Mount();
        var player = engine.Player!;
        player.OnMove = null;
        player.OnCollide = null;
        if (collectRing)
        {
            player.Position = new Vector3(engine.RingField!.WorldPosition(0), 0);
            engine.Step();
            Assert.Equal(1, engine.RingCount);
        }
        player.Position = new Vector3(engine.GoalPosition!.Value, 0);
        engine.Step();
        Assert.True(engine.ActClear);
        int rings = engine.RingCount;
        var result = engine.DamagePlayer();
        Assert.Equal(DamageOutcome.Ignored, result.Outcome);
        Assert.Equal(rings, result.RingsRemaining);
        for (int i = 0; i < 140; i++) engine.Step();
        Assert.Same(player, engine.Player);
        Assert.False(player.IsDead);
        Assert.False(player.IsDamaged);
        Assert.Equal(rings, engine.RingCount);
        Assert.Equal(0f, engine.DeathWaitTime);
        Assert.Equal(3, engine.Lives);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void LeavingGameOverReleasesItsPauseForTheNextScene(bool applyPauseBeforeExit)
    {
        var (engine, _) = Mount();
        for (int attempt = 0; attempt < 3; attempt++)
        {
            engine.DamagePlayer();
            for (int i = 0; i < 136; i++) engine.Step();
        }
        engine.DamagePlayer();
        for (int i = 0; i < 120; i++) engine.Step();
        Assert.True(engine.GameOver);
        int ticks = 0;
        engine.Scheduler.Create("session", _ => ticks++, GameEngine.PriorityObject, group: 9);
        if (applyPauseBeforeExit)
        {
            engine.Step();
            Assert.Equal(0, engine.Scheduler.PauseLevel);
            Assert.Equal(0, ticks);
        }
        engine.Events.RequestChange();
        engine.Step();
        Assert.Null(engine.Stage);
        Assert.False(engine.GameOver);
        Assert.Equal(-1, engine.Scheduler.PauseLevel);
        Assert.Equal(1, ticks);
    }

    [Fact]
    public void RestartKeepsItsArchiveSnapshotWhenTheContentSourceMutatesItsBuffer()
    {
        var (engine, content) = Mount();
        var player = engine.Player!;
        var stage = engine.Stage;
        int items = engine.ItemBoxes!.Count;
        content.CorruptRetainedArchive();
        content.RefuseReads = true;
        Assert.True(engine.RequestRestart());
        engine.Step();
        Assert.True(player.Destroyed);
        Assert.NotSame(player, engine.Player);
        Assert.Same(stage, engine.Stage);
        Assert.Equal(items, engine.ItemBoxes!.Count);
        Assert.Equal(items, engine.ItemBoxes.Remaining);
        Assert.Equal(2ul, engine.StageAttempt);
    }

    [Fact]
    public void DeathWaitPausesThenConsumesOneLifeAndRestartsAfterTheFade()
    {
        var (engine, _) = Mount();
        var player = engine.Player!;
        Assert.Equal(DamageOutcome.Death, engine.DamagePlayer().Outcome);
        Assert.False(engine.RequestRestart());
        for (int i = 0; i < 119; i++) engine.Step();
        Assert.Equal(119f, engine.DeathWaitTime);
        Assert.Equal(3, engine.Lives);
        engine.Scheduler.StartPause(0);
        for (int i = 0; i < 130; i++) engine.Step();
        Assert.Equal(119f, engine.DeathWaitTime);
        Assert.Same(player, engine.Player);
        engine.Scheduler.EndPause();
        engine.Step();
        Assert.Equal(120f, engine.DeathWaitTime);
        Assert.Equal(2, engine.Lives);
        Assert.Equal(0f, engine.RestartFadeOpacity);
        for (int i = 0; i < 15; i++) engine.Step();
        Assert.Same(player, engine.Player);
        Assert.Equal(1f, engine.RestartFadeOpacity);
        Assert.Equal(2, engine.Lives);
        engine.Step();
        Assert.True(player.Destroyed);
        Assert.False(engine.Player!.IsDead);
        Assert.Equal(2, engine.Lives);
        Assert.Equal(0f, engine.DeathWaitTime);
        Assert.Equal(0f, engine.RestartFadeOpacity);
    }

    [Fact]
    public void ZeroLivesStillRestartsAndUnderflowStopsUntilANewRunIsRequested()
    {
        var (engine, _) = Mount();
        for (int lives = 3; lives >= 0; lives--)
        {
            Assert.Equal(lives, engine.Lives);
            var player = engine.Player!;
            Assert.Equal(DamageOutcome.Death, engine.DamagePlayer().Outcome);
            for (int i = 0; i < 120; i++) engine.Step();
            Assert.Equal(lives - 1, engine.Lives);
            if (lives == 0)
            {
                Assert.True(engine.GameOver);
                for (int i = 0; i < 200; i++) engine.Step();
                Assert.Same(player, engine.Player);
                Assert.Equal(-1, engine.Lives);
            }
            else
            {
                Assert.False(engine.GameOver);
                for (int i = 0; i < 16; i++) engine.Step();
                Assert.NotSame(player, engine.Player);
                Assert.False(engine.Player!.IsDead);
            }
        }
        var attempt = engine.StageAttempt;
        Assert.True(engine.RequestRestart());
        engine.Step();
        Assert.Equal(attempt + 1, engine.StageAttempt);
        Assert.Equal(3, engine.Lives);
        Assert.False(engine.GameOver);
        Assert.False(engine.Player!.IsDead);
        Assert.Equal(-1, engine.Scheduler.PauseLevel);
    }

    [Fact]
    public void DestructionCannotResurrectTasksOrObjectsAcrossTheTwoManagers()
    {
        var (engine, _) = Mount();
        int destroyed = 0;
        engine.Scheduler.Create("old", _ => { }, 1, destructor: _ =>
            engine.Objects.Add(new GameObject { OnDestroy = _ => destroyed++ }),
            group: GameEngine.SceneGroup);
        engine.Player!.OnDestroy = _ => engine.Scheduler.Create("resurrected", _ =>
            throw new InvalidOperationException("old task survived"), 1, destructor: _ =>
                engine.Objects.Add(new GameObject { OnDestroy = _ => destroyed++ }),
            group: GameEngine.SceneGroup);
        Assert.True(engine.RequestRestart());
        engine.Step();
        Assert.Equal(2, destroyed);
        Assert.Single(engine.Objects.Objects);
        Assert.DoesNotContain(engine.Scheduler.Tasks, task => task.Name is "old" or "resurrected");
    }

    [Fact]
    public void RestartIsDeferredAndRetainsTheMountedStageAndSession()
    {
        var (engine, content) = Mount();
        var stage = engine.Stage;
        var collision = engine.Collision;
        var placements = engine.Placements;
        var player = engine.Player!;
        var spawn = player.Position;
        var tasks = engine.Scheduler.Tasks.ToArray();
        var rings = engine.RingField!;
        var boxes = engine.ItemBoxes!;
        var lands = engine.Lands;
        var water = engine.WaterAreas;
        var enemies = engine.HariSenbos;
        int destroyed = 0;
        player.OnDestroy = _ => destroyed++;
        rings.Collect(rings.WorldPosition(0));
        boxes.Check(boxes.PositionOf(0)).ToArray();
        player.InputX = 1;
        player.InputJump = true;
        player.InputDown = true;
        engine.ActArchive = "changed-after-mount";
        engine.SpawnCellX = 7;
        engine.SpawnCellY = 7;
        content.RefuseReads = true;
        int reads = content.Reads;
        var frame = engine.Frame;

        Assert.True(engine.RequestRestart());
        Assert.True(engine.RequestRestart());
        Assert.Same(player, engine.Player);
        Assert.False(player.Destroyed);
        engine.Step();

        Assert.Same(stage, engine.Stage);
        Assert.Same(collision, engine.Collision);
        Assert.Same(placements, engine.Placements);
        Assert.Equal(reads, content.Reads);
        Assert.Equal(frame + 1, engine.Frame);
        Assert.Equal(1ul, engine.StageFrame);
        Assert.Equal(2ul, engine.StageAttempt);
        Assert.Equal(3, engine.Lives);
        Assert.True(player.Destroyed);
        Assert.Equal(1, destroyed);
        Assert.NotSame(player, engine.Player);
        Assert.Equal(spawn, engine.Player!.Position);
        Assert.Equal(0, engine.Player.InputX);
        Assert.False(engine.Player.InputJump);
        Assert.False(engine.Player.InputDown);
        Assert.NotSame(rings, engine.RingField);
        Assert.Equal(0, engine.RingField!.Collected);
        Assert.NotSame(boxes, engine.ItemBoxes);
        Assert.Equal(1, engine.ItemBoxes!.Remaining);
        Assert.NotSame(lands, engine.Lands);
        Assert.NotSame(water, engine.WaterAreas);
        Assert.NotSame(enemies, engine.HariSenbos);
        Assert.Single(engine.Objects.Objects);
        Assert.All(tasks, task => Assert.True(task.Deleted));
        Assert.Equal(tasks.Select(task => task.Name), engine.Scheduler.Tasks.Select(task => task.Name));
        engine.Step();
        Assert.Equal(2ul, engine.StageAttempt);
        Assert.Equal(1, destroyed);
    }

    [Fact]
    public void RequestsFromTasksWaitUntilTheNextStepAndLeaveOtherGroupsRunning()
    {
        var (engine, _) = Mount();
        var player = engine.Player!;
        int ticks = 0;
        engine.Scheduler.Create("session", task =>
        {
            ticks++;
            if (ticks == 1) Assert.True(engine.RequestRestart());
        }, GameEngine.PriorityObject, group: 9);
        engine.Step();
        Assert.Same(player, engine.Player);
        Assert.Equal(1ul, engine.StageAttempt);
        engine.Step();
        Assert.NotSame(player, engine.Player);
        Assert.Equal(2, ticks);
        Assert.Equal(2ul, engine.StageAttempt);
        Assert.Single(engine.Scheduler.Tasks, task => task.Group == 9);
    }

    [Fact]
    public void ExitingTheStageDestroysItsObjectsAndDropsAttemptState()
    {
        var (engine, _) = Mount();
        var player = engine.Player!;
        var tasks = engine.Scheduler.Tasks.ToArray();
        int destroyed = 0;
        player.OnDestroy = _ => destroyed++;
        engine.Events.RequestChange();
        engine.Step();

        Assert.True(player.Destroyed);
        Assert.Equal(1, destroyed);
        Assert.Empty(engine.Objects.Objects);
        Assert.All(tasks, task => Assert.True(task.Deleted));
        Assert.Null(engine.Stage);
        Assert.Null(engine.Player);
        Assert.Null(engine.ItemBoxes);
        Assert.Null(engine.RingField);
        Assert.Null(engine.Lands);
        Assert.Null(engine.HariSenbos);
        engine.Step();
        Assert.Equal(1, destroyed);
    }

    [Fact]
    public void ADeadPlayerCannotCollectRingsBreakMonitorsOrClearTheAct()
    {
        var (engine, _) = Mount();
        var player = engine.Player!;
        Assert.Equal(DamageOutcome.Death, engine.DamagePlayer().Outcome);
        player.OnMove = null;
        player.OnCollide = null;

        var ring = engine.RingField!.WorldPosition(0);
        player.Position = new Vector3(ring, 0);
        engine.Step();
        Assert.Equal(0, engine.RingCount);
        Assert.Equal(0, engine.RingField.Collected);

        player.Position = new Vector3(engine.ItemBoxes!.PositionOf(0), 0);
        engine.Step();
        Assert.Equal(1, engine.ItemBoxes.Remaining);
        Assert.Equal(3, engine.Lives);

        player.Position = new Vector3(engine.GoalPosition!.Value, 0);
        engine.Step();
        Assert.False(engine.ActClear);
    }

    private static (GameEngine Engine, AttemptContent Content) Mount()
    {
        var content = new AttemptContent();
        var engine = new GameEngine(content);
        engine.Step();
        Assert.NotNull(engine.Player);
        return (engine, content);
    }

    private sealed class AttemptContent : IContentSource
    {
        public int Reads { get; private set; }
        public bool RefuseReads { get; set; }
        private byte[]? _retainedArchive;

        public void CorruptRetainedArchive() => _retainedArchive![0] = 0;

        public bool Exists(string path) => path is
            "G_ZONE1/MAP/ZONE11_MAP.AMB" or "G_ZONE1/MAP/ZONE1_M.AMB";

        public byte[] Read(string path)
        {
            Reads++;
            if (RefuseReads) throw new InvalidOperationException("attempt reread its content");
            return path switch
            {
                "G_ZONE1/MAP/ZONE11_MAP.AMB" => _retainedArchive ??= Archive(
                    ("ZONE11_ATTR_B.MP", Grid()),
                    ("ZONE11.EV", Events()),
                    ("ZONE11.RG", Block(2, new byte[] { 120, 176 }))),
                "G_ZONE1/MAP/ZONE1_M.AMB" => Archive(),
                _ => throw new FileNotFoundException(path),
            };
        }

        public IEnumerable<string> List(string directory, string suffix) => [];

        private static byte[] Grid()
        {
            var data = new byte[4 + 8 * 8 * 2];
            BinaryPrimitives.WriteUInt16LittleEndian(data, 8);
            BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(2), 8);
            for (int x = 0; x < 8; x++)
                BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(4 + (3 * 8 + x) * 2), 1);
            return data;
        }

        private static byte[] Events()
        {
            var rows = new[] { (64, 128, 443), (200, 192, 67), (240, 192, 520) };
            var records = new byte[rows.Length * EventPlacements.RecordStride];
            for (int i = 0; i < rows.Length; i++)
            {
                var row = rows[i];
                var record = records.AsSpan(i * EventPlacements.RecordStride);
                record[0] = (byte)row.Item1;
                record[1] = (byte)row.Item2;
                BinaryPrimitives.WriteUInt16LittleEndian(record[2..], (ushort)row.Item3);
            }
            return Block(EventPlacements.RecordStride, records);
        }

        private static byte[] Block(int stride, byte[] records)
        {
            var data = new byte[10 + records.Length];
            BinaryPrimitives.WriteUInt16LittleEndian(data, 1);
            BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(2), 1);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(4), 8);
            BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(8), (ushort)(records.Length / stride));
            records.CopyTo(data, 10);
            return data;
        }

        private static byte[] Archive(params (string Name, byte[] Data)[] entries)
        {
            int names = 32 + entries.Length * 16;
            int body = names + entries.Length * 32;
            var data = new byte[body + entries.Sum(entry => entry.Data.Length)];
            "#AMB"u8.CopyTo(data);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(16), entries.Length);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(20), 32);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(28), names);
            for (int i = 0; i < entries.Length; i++)
            {
                BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(32 + i * 16), body);
                BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(36 + i * 16), entries[i].Data.Length);
                Encoding.ASCII.GetBytes(entries[i].Name).CopyTo(data, names + i * 32);
                entries[i].Data.CopyTo(data, body);
                body += entries[i].Data.Length;
            }
            return data;
        }
    }
}
