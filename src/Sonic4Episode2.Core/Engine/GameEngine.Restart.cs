namespace Sonic4Episode2.Core.Engine;

public sealed partial class GameEngine
{
    private bool _restartRequested;
    private bool _restarting;
    private bool _newRunRequested;
    private float _restartFadeElapsed = -1f;
    private const float RestartFadeDuration = 15f;

    public float DeathWaitTime { get; private set; }
    public bool GameOver { get; private set; }
    public float RestartFadeOpacity => Math.Clamp(_restartFadeElapsed / RestartFadeDuration, 0f, 1f);

    public bool RequestRestart()
    {
        if (Stage is null || Player is null || (Player.IsDead && !GameOver) || _restarting)
            return false;
        _newRunRequested = GameOver;
        _restartRequested = true;
        return true;
    }

    private void ResetDeathWait()
    {
        DeathWaitTime = 0;
        GameOver = false;
        _restartFadeElapsed = -1;
    }

    private void UpdateDeathWait()
    {
        if (Player is null || GameOver || ActClear || Scheduler.PauseLevel >= 0) return;
        if (_restartFadeElapsed >= 0)
        {
            _restartFadeElapsed = Math.Min(_restartFadeElapsed + 1, RestartFadeDuration);
            if (_restartFadeElapsed >= RestartFadeDuration) _restartRequested = true;
            return;
        }

        var result = DeathWait.Tick(new(DeathWaitTime, Lives), Player.IsDead, suppressed: false);
        DeathWaitTime = result.State.Elapsed;
        Lives = result.State.RemainingLives;
        if (result.Action == DeathWaitAction.Restart)
        {
            _restartFadeElapsed = 0;
            Status = $"RESTART - {StageName}";
        }
        else if (result.Action == DeathWaitAction.GameOver)
        {
            GameOver = true;
            Status = "GAME OVER - press R to start a new run";
            Scheduler.StartPause(0);
        }
    }

    private void ApplyPendingRestart()
    {
        if (!_restartRequested) return;
        _restartRequested = false;
        if (Stage is null || _mountedActData is null) return;
        _restarting = true;
        try
        {
            ClearStageAttempt();
            if (_newRunRequested) Lives = 3;
            _newRunRequested = false;
            Scheduler.EndPause();
            StartStageAttempt();
            Status = _mountedStatus;
        }
        finally
        {
            _restarting = false;
        }
    }
}
