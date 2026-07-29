// Stage material effect.
//
// Our own implementation of the material model recovered from Episode II — see
// docs/ORACLES.md. Nothing here is copied from the game; it is written against
// recovered facts:
//
//   * a material carries an ambient and a diffuse RGBA (u_FrontMaterial, two
//     float4 registers — confirmed independently from the model data and from
//     the shaders' own CTAB tables),
//   * the diffuse modulates the base texture and its alpha is per-material
//     transparency,
//   * lighting is PER PIXEL, not per vertex: u_LightSource appears in 676 pixel
//     shaders against 19 vertex shaders,
//   * a scene ambient sits under one parallel light.
//
// Multi-texture is deliberately not here yet. The capture shows the engine uses
// four sampler slots in three nested configurations, and that lands next.

#if OPENGL
    #define VS_SHADERMODEL vs_3_0
    #define PS_SHADERMODEL ps_3_0
#else
    #define VS_SHADERMODEL vs_4_0_level_9_1
    #define PS_SHADERMODEL ps_4_0_level_9_1
#endif

float4x4 WorldViewProjection;

// Material terms, per draw batch.
float4 MaterialDiffuse = float4(1, 1, 1, 1);
float3 MaterialAmbient = float3(0.3, 0.3, 0.3);

// One parallel light. Direction points *from* the light toward the scene.
float3 LightDirection = float3(0.3, 0.6, 0.75);
float3 LightDiffuse   = float3(0.85, 0.85, 0.85);

texture BaseTexture;
sampler2D BaseSampler = sampler_state
{
    Texture   = <BaseTexture>;
    MinFilter = Linear;
    MagFilter = Linear;
    MipFilter = Linear;
    AddressU  = Wrap;
    AddressV  = Wrap;
};

struct VSInput
{
    float4 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : POSITION0;
    float2 TexCoord : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
};

VSOutput MainVS(VSInput input)
{
    VSOutput output;
    output.Position = mul(input.Position, WorldViewProjection);
    output.TexCoord = input.TexCoord;
    // Tiles are placed by translation only, so the normal carries through
    // unrotated. Posed and skinned geometry is transformed on the CPU before it
    // reaches here (StageAssembler), so it arrives already in world space.
    output.Normal = input.Normal;
    return output;
}

float4 MainPS(VSOutput input) : COLOR0
{
    float4 texel = tex2D(BaseSampler, input.TexCoord);
    float4 base  = texel * MaterialDiffuse;

    float3 n = normalize(input.Normal);
    float  ndotl = saturate(dot(n, -normalize(LightDirection)));

    float3 lit = base.rgb * (MaterialAmbient + LightDiffuse * ndotl);
    return float4(lit, base.a);
}

technique StageTechnique
{
    pass P0
    {
        VertexShader = compile VS_SHADERMODEL MainVS();
        PixelShader  = compile PS_SHADERMODEL MainPS();
    }
}
