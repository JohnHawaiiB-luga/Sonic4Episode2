using Microsoft.Xna.Framework.Input.Touch;
using Sonic4Episode2.Core.Engine;
using Vector2 = System.Numerics.Vector2;

namespace Sonic4Episode2.Android;

/// <summary>
/// Drives the player from the touchscreen through a <see cref="VirtualPad"/>.
/// </summary>
/// <remarks>
/// All the layout and dead-zone logic lives in <c>VirtualPad</c>, in the core
/// library, where it is unit tested without a device. This class does nothing but
/// hand it this frame's touch points, which is the only part that genuinely needs
/// Android.
/// </remarks>
public sealed class TouchInput : IInputSource
{
    private VirtualPad? _pad;
    private int _width, _height;
    private readonly List<Vector2> _points = [];

    public void Apply(Player player, int screenWidth, int screenHeight)
    {
        if (_pad is null || screenWidth != _width || screenHeight != _height)
        {
            _pad = new VirtualPad(screenWidth, screenHeight);
            _width = screenWidth;
            _height = screenHeight;
        }

        _points.Clear();
        foreach (var touch in TouchPanel.GetState())
            if (touch.State is TouchLocationState.Pressed or TouchLocationState.Moved)
                _points.Add(new Vector2(touch.Position.X, touch.Position.Y));

        _pad.Update(_points);
        _pad.ApplyTo(player);
    }
}
