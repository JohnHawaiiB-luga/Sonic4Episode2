namespace Sonic4Episode2.Core.Engine;

public enum DeathWaitAction
{
    None,
    Restart,
    GameOver,
}

public readonly record struct DeathWaitState(float Elapsed, int RemainingLives);
public readonly record struct DeathWaitResult(DeathWaitState State, DeathWaitAction Action);

public static class DeathWait
{
    public const float Duration = 120f;

    public static DeathWaitResult Tick(DeathWaitState state, bool dead, bool suppressed,
                                       float timeStep = 1f)
    {
        if (!float.IsFinite(state.Elapsed) || state.Elapsed < 0 ||
            !float.IsFinite(timeStep) || timeStep < 0 ||
            state.RemainingLives < -1 || (state.RemainingLives == -1 && state.Elapsed < Duration))
            throw new ArgumentOutOfRangeException(nameof(state));
        if (!dead || suppressed || state.Elapsed >= Duration)
            return new(state, DeathWaitAction.None);

        float elapsed = state.Elapsed + timeStep;
        if (!float.IsFinite(elapsed)) throw new ArgumentOutOfRangeException(nameof(timeStep));
        if (elapsed < Duration)
            return new(new(elapsed, state.RemainingLives), DeathWaitAction.None);

        int lives = state.RemainingLives - 1;
        return new(new(elapsed, lives), lives < 0 ? DeathWaitAction.GameOver : DeathWaitAction.Restart);
    }
}
