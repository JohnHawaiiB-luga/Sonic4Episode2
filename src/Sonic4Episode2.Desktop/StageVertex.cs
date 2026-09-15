using System.Runtime.InteropServices;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace Sonic4Episode2.Desktop;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
internal readonly struct StageVertex : IVertexType
{
    public readonly Vector3 Position;
    public readonly Vector3 Normal;
    public readonly Vector2 TextureCoordinate;
    public readonly Color Color;

    public static readonly VertexDeclaration VertexDeclaration = new(
        new VertexElement(0, VertexElementFormat.Vector3, VertexElementUsage.Position, 0),
        new VertexElement(12, VertexElementFormat.Vector3, VertexElementUsage.Normal, 0),
        new VertexElement(24, VertexElementFormat.Vector2, VertexElementUsage.TextureCoordinate, 0),
        new VertexElement(32, VertexElementFormat.Color, VertexElementUsage.Color, 0));

    VertexDeclaration IVertexType.VertexDeclaration => VertexDeclaration;

    public StageVertex(Vector3 position, Vector3 normal, Vector2 textureCoordinate)
        : this(position, normal, textureCoordinate, Color.White) { }

    public StageVertex(Vector3 position, Vector3 normal, Vector2 textureCoordinate, Color color)
    {
        Position = position;
        Normal = normal;
        TextureCoordinate = textureCoordinate;
        Color = color;
    }

    public static Color ReadColor(IReadOnlyList<byte> colors, int vertex)
    {
        int offset = vertex * 4;
        return offset + 3 < colors.Count
            ? new Color(colors[offset], colors[offset + 1], colors[offset + 2], colors[offset + 3])
            : Color.White;
    }

    public static Vector2 ReadTextureCoordinate(IReadOnlyList<float> coordinates, int vertex)
        => new(coordinates[vertex * 2], coordinates[vertex * 2 + 1]);
}
