using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Desktop;

internal static class MaterialBlendCheck
{
    public static void Run(GraphicsDevice device, Effect stageEffect, BasicEffect basicEffect,
                           Action<MaterialKey> setBlend)
    {
        using var effect = stageEffect.Clone();
        using var basic = (BasicEffect)basicEffect.Clone();
        using var target = new RenderTarget2D(device, 8, 8);
        using var texture = new Texture2D(device, 1, 1);
        var targets = device.GetRenderTargets();
        var viewport = device.Viewport;
        var blend = device.BlendState;
        var depth = device.DepthStencilState;
        var rasterizer = device.RasterizerState;
        var sampler = device.SamplerStates[0];
        var boundTexture = device.Textures[0];
        var background = new Color(24, 36, 48);
        var vertices = new[]
        {
            new StageVertex(new(-1, -1, 0), Vector3.Backward, Vector2.Zero),
            new StageVertex(new(1, -1, 0), Vector3.Backward, Vector2.Zero),
            new StageVertex(new(-1, 1, 0), Vector3.Backward, Vector2.Zero),
            new StageVertex(new(1, 1, 0), Vector3.Backward, Vector2.Zero),
        };
        int[] indices = [0, 1, 2, 2, 1, 3];
        var pixels = new Color[64];
        int cases = 0;
        int stageCases = 0;
        int basicCases = 0;
        int mismatches = 0;
        int maximumError = 0;
        try
        {
            effect.CurrentTechnique = effect.Techniques["StageTechnique"];
            effect.Parameters["WorldViewProjection"].SetValue(Matrix.Identity);
            effect.Parameters["MaterialAmbient"].SetValue(Vector3.One);
            effect.Parameters["LightDirection"].SetValue(Vector3.Forward);
            effect.Parameters["LightDiffuse"].SetValue(Vector3.Zero);
            effect.Parameters["LightingEnabled"].SetValue(1f);
            effect.Parameters["AlphaComparison"].SetValue(8f);
            basic.World = basic.View = basic.Projection = Matrix.Identity;
            basic.LightingEnabled = false;
            basic.DiffuseColor = Vector3.One;
            basic.Alpha = 1f;
            basic.Texture = texture;
            device.DepthStencilState = DepthStencilState.None;
            device.RasterizerState = RasterizerState.CullNone;
            foreach (bool basicPath in new[] { false, true })
            foreach (var mode in new[] { MaterialBlend.Alpha, MaterialBlend.Additive })
            foreach (byte alpha in new byte[] { 0, 64, 128, 255 })
            foreach (float opacity in new[] { 0f, 0.25f, 1f })
            foreach (var color in new[] { Color.White, new Color(64, 128, 192, 128),
                new Color(199, 91, 0, 255), new Color(0, 0, 0, 255), new Color(199, 91, 0, 0) })
            {
                if (basicPath && opacity != 1f) continue;
                var source = new Color((byte)200, (byte)120, (byte)80, alpha);
                var material = new MaterialKey(default, mode, (0.5f, 0.75f, 1f, opacity));
                for (int i = 0; i < vertices.Length; i++)
                    vertices[i] = new StageVertex(vertices[i].Position, vertices[i].Normal,
                                                 vertices[i].TextureCoordinate, color);
                texture.SetData(new[] { source });
                device.SetRenderTarget(target);
                device.Clear(background);
                setBlend(material);
                effect.Parameters["MaterialDiffuse"].SetValue(new Vector4(0.5f, 0.75f, 1f, opacity));
                Effect active = basicPath ? basic : effect;
                foreach (var pass in active.CurrentTechnique.Passes)
                {
                    pass.Apply();
                    device.Textures[0] = texture;
                    device.SamplerStates[0] = SamplerState.PointClamp;
                    device.DrawUserIndexedPrimitives(PrimitiveType.TriangleList, vertices, 0, 4, indices, 0, 2);
                }
                device.SetRenderTargets(targets);
                target.GetData(pixels);
                float coverage = alpha / 255f * opacity * color.A / 255f;
                float destination = mode == MaterialBlend.Additive ? 1f : 1f - coverage;
                var rgb = basicPath ? new Vector3(200f, 120f, 80f) : new Vector3(100f, 90f, 80f);
                var vertexRgb = new Vector3(color.R, color.G, color.B) / 255f;
                var expected = rgb * vertexRgb * coverage +
                               new Vector3(background.R, background.G, background.B) * destination;
                foreach (var pixel in pixels)
                {
                    int error = Math.Max(Math.Abs(pixel.R - Math.Clamp((int)MathF.Round(expected.X), 0, 255)),
                        Math.Max(Math.Abs(pixel.G - Math.Clamp((int)MathF.Round(expected.Y), 0, 255)),
                                 Math.Abs(pixel.B - Math.Clamp((int)MathF.Round(expected.Z), 0, 255))));
                    maximumError = Math.Max(maximumError, error);
                    if (error > 2) mismatches++;
                }
                cases++;
                if (basicPath) basicCases++; else stageCases++;
            }
            CheckTextureCoordinates(device, effect, basic, target, targets);
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
            type = "material_blend_check", passed = mismatches == 0, cases, stageCases, basicCases,
            pixels = cases * pixels.Length, mismatches, maximumError
        }));
        if (mismatches != 0)
            throw new InvalidDataException($"material blend check failed: {mismatches} pixels, maximum error {maximumError}");
    }

    private static void CheckTextureCoordinates(GraphicsDevice device, Effect effect, BasicEffect basic,
                                                RenderTarget2D target, RenderTargetBinding[] targets)
    {
        using var texture = new Texture2D(device, 2, 2);
        Color[] corners = [Color.Red, Color.Lime, Color.Blue, Color.Yellow];
        texture.SetData(corners);
        float[] coordinates = [0, 1, 1, 1, 0, 0, 1, 0];
        Vector3[] positions = [new(-1, -1, 0), new(1, -1, 0), new(-1, 1, 0), new(1, 1, 0)];
        var vertices = new StageVertex[4];
        for (int i = 0; i < vertices.Length; i++)
            vertices[i] = new StageVertex(positions[i], Vector3.Backward,
                                         StageVertex.ReadTextureCoordinate(coordinates, i));
        int[] indices = [0, 1, 2, 2, 1, 3];
        var pixels = new Color[64];
        effect.Parameters["MaterialDiffuse"].SetValue(Vector4.One);
        basic.Texture = texture;
        device.BlendState = BlendState.Opaque;
        int mismatches = 0;
        foreach (Effect active in new Effect[] { effect, basic })
        {
            device.SetRenderTarget(target);
            device.Clear(Color.Black);
            foreach (var pass in active.CurrentTechnique.Passes)
            {
                pass.Apply();
                device.Textures[0] = texture;
                device.SamplerStates[0] = SamplerState.PointClamp;
                device.DrawUserIndexedPrimitives(PrimitiveType.TriangleList, vertices, 0, 4, indices, 0, 2);
            }
            device.SetRenderTargets(targets);
            target.GetData(pixels);
            for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (pixels[y * 8 + x] != corners[(y / 4) * 2 + x / 4]) mismatches++;
        }
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
        {
            type = "material_uv_check", passed = mismatches == 0, cases = 2, pixels = 128, mismatches
        }));
        if (mismatches != 0)
            throw new InvalidDataException($"material UV check failed: {mismatches} pixels");
    }
}
