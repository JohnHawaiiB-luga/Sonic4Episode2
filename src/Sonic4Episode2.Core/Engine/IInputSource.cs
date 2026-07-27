namespace Sonic4Episode2.Core.Engine;

/// <summary>
/// Where a player's input comes from on this platform.
/// </summary>
/// <remarks>
/// The desktop head reads a keyboard, the Android head reads touches through a
/// <see cref="VirtualPad"/>. Both heads otherwise share the same renderer, and
/// this is what lets them: the only thing that genuinely differs between a phone
/// and a PC here is how a direction gets expressed.
/// </remarks>
public interface IInputSource
{
    /// <summary>Sets this frame's input on the player.</summary>
    /// <param name="player">The player to drive.</param>
    /// <param name="screenWidth">Backbuffer width, for layouts that scale.</param>
    /// <param name="screenHeight">Backbuffer height.</param>
    void Apply(Player player, int screenWidth, int screenHeight);
}
