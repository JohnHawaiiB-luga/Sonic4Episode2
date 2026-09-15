namespace Sonic4Episode2.Core.Assets;

public readonly record struct MaterialRenderState(
    bool LightingEnabled,
    bool BlendEnabled,
    bool DepthTestEnabled,
    bool DepthWriteEnabled,
    byte ColorWriteMask,
    byte AlphaComparison,
    byte AlphaReference,
    byte DepthComparison)
{
    public static MaterialRenderState Default { get; } = new(
        LightingEnabled: true,
        BlendEnabled: true,
        DepthTestEnabled: true,
        DepthWriteEnabled: true,
        ColorWriteMask: 0x0F,
        AlphaComparison: 8,
        AlphaReference: 0,
        DepthComparison: 4);
}
