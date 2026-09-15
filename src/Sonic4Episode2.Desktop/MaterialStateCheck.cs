using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Desktop;

internal static class MaterialStateCheck
{
    public static void Run(GraphicsDevice device, Effect stageEffect, MaterialRenderStates states)
    {
        using var effect = stageEffect.Clone();
        using var target = new RenderTarget2D(device, 8, 8, false, SurfaceFormat.Color, DepthFormat.Depth24);
        using var texture = new Texture2D(device, 1, 1);
        var targets = device.GetRenderTargets();
        var viewport = device.Viewport;
        var blend = device.BlendState;
        var depth = device.DepthStencilState;
        var rasterizer = device.RasterizerState;
        var sampler = device.SamplerStates[0];
        var boundTexture = device.Textures[0];
        var background = new Color(10, 20, 30, 40);
        var colour = new Color(200, 100, 50, 255);
        var vertices = new StageVertex[4];
        int[] indices = [0, 1, 2, 2, 1, 3];
        var pixels = new Color[64];
        int cases = 0, mismatches = 0, maximumError = 0;
        string? firstFailure = null;
        var solid = MaterialRenderState.Default with
        {
            LightingEnabled = false, BlendEnabled = false, DepthTestEnabled = false,
        };

        void Begin()
        {
            device.SetRenderTarget(target);
            device.Clear(ClearOptions.Target | ClearOptions.DepthBuffer, background, 1f, 0);
        }

        void Draw(Color source, MaterialRenderState state, float z = 0.5f)
        {
            texture.SetData(new[] { source });
            vertices[0] = new StageVertex(new(-1, -1, z), Vector3.Backward, Vector2.Zero);
            vertices[1] = new StageVertex(new(1, -1, z), Vector3.Backward, Vector2.Zero);
            vertices[2] = new StageVertex(new(-1, 1, z), Vector3.Backward, Vector2.Zero);
            vertices[3] = new StageVertex(new(1, 1, z), Vector3.Backward, Vector2.Zero);
            var material = MaterialKey.FromBase(null) with { RenderState = state };
            states.Apply(device, material);
            MaterialRenderStates.ConfigureEffect(effect, material);
            foreach (var pass in effect.CurrentTechnique.Passes)
            {
                pass.Apply();
                device.Textures[0] = texture;
                device.SamplerStates[0] = SamplerState.PointClamp;
                device.DrawUserIndexedPrimitives(PrimitiveType.TriangleList, vertices, 0, 4, indices, 0, 2);
            }
        }

        void Check(Color expected, string label)
        {
            device.SetRenderTargets(targets);
            target.GetData(pixels);
            foreach (var pixel in pixels)
            {
                int error = Math.Max(Math.Max(Math.Abs(pixel.R - expected.R), Math.Abs(pixel.G - expected.G)),
                                    Math.Max(Math.Abs(pixel.B - expected.B), Math.Abs(pixel.A - expected.A)));
                maximumError = Math.Max(maximumError, error);
                if (error > 1)
                {
                    mismatches++;
                    firstFailure ??= label;
                }
            }
            cases++;
        }

        try
        {
            effect.CurrentTechnique = effect.Techniques["StageTechnique"];
            effect.Parameters["WorldViewProjection"].SetValue(Matrix.Identity);
            effect.Parameters["MaterialAmbient"].SetValue(new Vector3(0.25f));
            effect.Parameters["LightDiffuse"].SetValue(Vector3.Zero);
            effect.Parameters["LightDirection"].SetValue(Vector3.Forward);
            device.RasterizerState = RasterizerState.CullNone;
            foreach (byte comparison in Enumerable.Range(1, 8).Select(value => (byte)value))
            foreach (byte reference in new byte[] { 0, 128, 178, 247, 255 })
            foreach (byte alpha in new byte[] { 0, 127, 128, 129, 177, 178, 179, 246, 247, 248, 254, 255 })
            {
                Begin();
                var source = new Color(colour.R, colour.G, colour.B, alpha);
                Draw(source, solid with { AlphaComparison = comparison, AlphaReference = reference });
                Check(Compare(comparison, alpha, reference) ? source : background,
                      $"alpha {comparison}/{reference}/{alpha}");
            }
            for (byte mask = 0; mask < 16; mask++)
            {
                Begin();
                Draw(colour, solid with { ColorWriteMask = mask });
                Check(new Color((mask & 1) != 0 ? colour.R : background.R,
                                (mask & 2) != 0 ? colour.G : background.G,
                                (mask & 4) != 0 ? colour.B : background.B,
                                (mask & 8) != 0 ? colour.A : background.A), $"colour mask {mask}");
            }
            foreach (bool lighting in new[] { false, true })
            {
                Begin();
                Draw(colour, solid with { LightingEnabled = lighting });
                Check(lighting ? new Color(50, 25, 13, 255) : colour, $"lighting {lighting}");
            }
            foreach (bool test in new[] { false, true })
            foreach (bool write in new[] { false, true })
            foreach (byte alpha in new byte[] { 127, 128, 129, 255 })
            {
                Begin();
                Draw(new Color(colour.R, colour.G, colour.B, alpha), solid with
                {
                    DepthTestEnabled = test, DepthWriteEnabled = write, ColorWriteMask = 0,
                    AlphaComparison = 5, AlphaReference = 128,
                }, 0.25f);
                Draw(colour, solid with { DepthTestEnabled = true }, 0.75f);
                Check(test && write && alpha > 128 ? background : colour, $"depth mask {test}/{write}/{alpha}");
            }
            foreach (byte comparison in Enumerable.Range(1, 8).Select(value => (byte)value))
            foreach (float z in new[] { 0.25f, 0.5f, 0.75f })
            {
                Begin();
                Draw(colour, solid with { DepthTestEnabled = true, ColorWriteMask = 0 });
                Draw(colour, solid with { DepthTestEnabled = true, DepthComparison = comparison }, z);
                Check(Compare(comparison, z, 0.5f) ? colour : background, $"depth comparison {comparison}/{z}");
            }
        }
        finally
        {
            device.SetRenderTargets(targets);
            device.Viewport = viewport;
            device.BlendState = blend;
            device.DepthStencilState = depth;
            device.RasterizerState = rasterizer;
            device.SamplerStates[0] = sampler;
            device.Textures[0] = boundTexture;
        }
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
        {
            type = "material_state_check", passed = mismatches == 0, cases,
            pixels = cases * pixels.Length, mismatches, maximumError, firstFailure,
        }));
        if (mismatches != 0)
            throw new InvalidDataException($"material state check failed: {firstFailure}, {mismatches} pixels");
    }

    private static bool Compare(byte comparison, float value, float reference) => comparison switch
    {
        1 => false, 2 => value < reference, 3 => value == reference, 4 => value <= reference,
        5 => value > reference, 6 => value != reference, 7 => value >= reference, 8 => true,
        _ => throw new InvalidDataException("invalid comparison in rendering check"),
    };
}
