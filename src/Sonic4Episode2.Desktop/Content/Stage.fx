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
// MULTI-TEXTURE. A material binds up to three live textures, by role, and the
// role is in the stage flag word rather than the array position (verified across
// all 9,767 materials; see NnTextureStage). The register numbers the game's own
// shaders use are NOT reproduced: reading every shader's CTAB shows the slot a
// texture lands in is a property of that shader permutation, not of the texture,
// so this effect assigns its own — base s0, environment s1.

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

// How much of the environment map reaches the surface — the engine's
// u_TexDualParaboloidLevel.x, whose role is recovered (doubling it exactly
// doubles the reflection in all 31 shaders that sample one) but whose runtime
// VALUE was never observed. So the term is real and the number is ours; it is
// flagged, and STAGE_ENV overrides it at runtime for sweeping.
float EnvironmentStrength = 0.35f;

// Bound to sampler slots on the device, not through sampler_state. Under
// MojoShader the sampler_state form did not pick the texture up, which made every
// texel read black and took the whole shader down with it — the flat-colour
// diagnostic proved the geometry and transform were fine while this was the fault.
sampler2D BaseSampler : register(s0);
sampler2D EnvSampler  : register(s1);

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

// The lit base colour, shared by every technique.
float4 ShadeBase(VSOutput input, out float3 normal)
{
    float4 texel = tex2D(BaseSampler, input.TexCoord);
    float4 base  = texel * MaterialDiffuse;

    normal = normalize(input.Normal);
    float ndotl = saturate(dot(normal, -normalize(LightDirection)));

    return float4(base.rgb * (MaterialAmbient + LightDiffuse * ndotl), base.a);
}

float4 MainPS(VSOutput input) : COLOR0
{
    float3 n;
    return ShadeBase(input, n);
}

// Environment-mapped: 1,322 stages across the build carry a reflection map, and
// every one of them was being discarded before the batch key carried it.
//
// RECOVERED, by disassembling the game's own ps_3_0 bytecode (all 922 decode).
// The engine's environment path is a DUAL PARABOLOID map, and the reflection is
// computed in the pixel shader, not handed down from the vertex shader:
//
//     I   = normalize(eyePosition)
//     R   = I - 2*dot(I,N)*N
//     R'  = u_DualParaboloidMatrix * R
//     den = |R'.z| + 1
//     s   = ( R'.x/den + 1) * 0.5
//     t   = (-R'.y/den + 1) * 0.5
//     u   = (R'.z >= 0 ? -0.5 : +0.5) * s
//
// verified numerically on all 31 shaders that sample it, across both hemisphere
// branches, against 9 different instruction schedulings that all collapse to
// this one formula.
//
// Two simplifications, both sound here and both deliberate. The stage camera is
// orthographic down -Z, so the incident vector is the constant (0,0,-1) and no
// eye position is needed. And u_DualParaboloidMatrix is taken as identity: its
// shape is known (a 3x3 rotating view space into the map's frame) but its
// runtime value was never observed, so identity is the honest default and is
// flagged as an assumption rather than a recovered value.
//
// One sign is NOT independently confirmed: which hemisphere the +0.5 half of the
// atlas holds rests on the documented D3D9 `cmp` rule rather than on measurement.
// If reflections ever look mirrored between up-facing and down-facing surfaces,
// this is the line to flip.
float4 EnvironmentPS(VSOutput input) : COLOR0
{
    float3 n;
    float4 lit = ShadeBase(input, n);

    // Orthographic: the incident vector is constant, so R reduces to this.
    float3 r = float3(0, 0, -1) + 2.0f * n.z * n;

    float den = abs(r.z) + 1.0f;
    float s = ( r.x / den + 1.0f) * 0.5f;
    float t = (-r.y / den + 1.0f) * 0.5f;
    float u = (r.z >= 0.0f ? -0.5f : 0.5f) * s;

    float3 reflection = tex2D(EnvSampler, float2(u, t)).rgb;

    // ADDITIVE, and that part IS recovered — unanimous across all 31 env
    // shaders, confirmed both structurally (every chain ends in one mad with the
    // environment as the addend) and numerically (the gain is independent of the
    // base texel, which is the signature of an add rather than a multiply). The
    // environment never touches alpha, in 31 of 31.
    //
    // The engine scales it by envMask.r * u_TexDualParaboloidLevel.x. Our
    // materials carry no mask stage, which is the case the engine also has — 3
    // of its shaders drop the mask and use the level alone. EnvironmentStrength
    // is that level, and its value is still ours, not recovered.
    return float4(lit.rgb + reflection * EnvironmentStrength, lit.a);
}

technique StageTechnique
{
    pass P0
    {
        VertexShader = compile VS_SHADERMODEL MainVS();
        PixelShader  = compile PS_SHADERMODEL MainPS();
    }
}

technique StageEnvironment
{
    pass P0
    {
        VertexShader = compile VS_SHADERMODEL MainVS();
        PixelShader  = compile PS_SHADERMODEL EnvironmentPS();
    }
}

// Diagnostic: ignores all shading and returns a fixed colour. If the stage draws
// magenta with this, the transform and rasterisation are fine and the fault is in
// shading or sampling. If it stays black, geometry is not reaching the raster at
// all, which points at the matrix.
float4 FlatPS(VSOutput input) : COLOR0
{
    return float4(1, 0, 1, 1);
}

technique DiagnosticFlat
{
    pass P0
    {
        VertexShader = compile VS_SHADERMODEL MainVS();
        PixelShader  = compile PS_SHADERMODEL FlatPS();
    }
}
