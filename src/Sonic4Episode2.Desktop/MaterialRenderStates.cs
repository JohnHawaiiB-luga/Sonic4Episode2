using Microsoft.Xna.Framework.Graphics;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Desktop;

internal sealed class MaterialRenderStates : IDisposable
{
    private readonly Dictionary<(bool, MaterialBlend, byte), BlendState> _blends = [];
    private readonly Dictionary<(bool, bool, byte), DepthStencilState> _depths = [];

    public void Apply(GraphicsDevice device, MaterialKey material)
    {
        var state = material.RenderState;
        var blendKey = (state.BlendEnabled, material.Blend, state.ColorWriteMask);
        if (!_blends.TryGetValue(blendKey, out var blend))
        {
            var source = state.BlendEnabled ? Blend.SourceAlpha : Blend.One;
            var destination = !state.BlendEnabled ? Blend.Zero
                : material.IsAdditive ? Blend.One : Blend.InverseSourceAlpha;
            blend = new BlendState
            {
                ColorSourceBlend = source, AlphaSourceBlend = source,
                ColorDestinationBlend = destination, AlphaDestinationBlend = destination,
                ColorWriteChannels = (ColorWriteChannels)state.ColorWriteMask,
            };
            _blends.Add(blendKey, blend);
        }
        var depthKey = (state.DepthTestEnabled, state.DepthWriteEnabled, state.DepthComparison);
        if (!_depths.TryGetValue(depthKey, out var depth))
        {
            depth = new DepthStencilState
            {
                DepthBufferEnable = state.DepthTestEnabled,
                DepthBufferWriteEnable = state.DepthTestEnabled && state.DepthWriteEnabled,
                DepthBufferFunction = Comparison(state.DepthComparison),
            };
            _depths.Add(depthKey, depth);
        }
        device.BlendState = blend;
        device.DepthStencilState = depth;
    }

    public static void ConfigureEffect(Effect effect, MaterialKey material)
    {
        var state = material.RenderState;
        var diffuse = material.Diffuse;
        effect.Parameters["MaterialDiffuse"].SetValue(new Microsoft.Xna.Framework.Vector4(
            diffuse.R, diffuse.G, diffuse.B, diffuse.A));
        effect.Parameters["LightingEnabled"].SetValue(state.LightingEnabled ? 1f : 0f);
        effect.Parameters["AlphaComparison"].SetValue((float)state.AlphaComparison);
        effect.Parameters["AlphaReference"].SetValue((float)state.AlphaReference);
    }

    private static CompareFunction Comparison(byte value) => value switch
    {
        1 => CompareFunction.Never,
        2 => CompareFunction.Less,
        3 => CompareFunction.Equal,
        4 => CompareFunction.LessEqual,
        5 => CompareFunction.Greater,
        6 => CompareFunction.NotEqual,
        7 => CompareFunction.GreaterEqual,
        8 => CompareFunction.Always,
        _ => throw new InvalidDataException("unsupported material depth comparison"),
    };

    public void Dispose()
    {
        foreach (var blend in _blends.Values) blend.Dispose();
        foreach (var depth in _depths.Values) depth.Dispose();
        _blends.Clear();
        _depths.Clear();
    }
}
