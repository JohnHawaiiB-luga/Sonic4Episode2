using System.Buffers.Binary;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Decoder for the game's DirectDraw Surface textures — DXT1/3/5 plus the
/// uncompressed variants — producing straight RGBA8.
/// </summary>
/// <remarks>
/// 2,853 textures in the build, all of which decode. Decoding here rather than
/// leaning on a library keeps <c>Core</c> dependency-free and gives the eventual
/// mobile heads a fallback for devices without S3TC.
/// <para>
/// DXT1 encodes one-bit alpha by endpoint ordering: when colour0 &lt;= colour1
/// the fourth palette entry is transparent black rather than an interpolated
/// colour.
/// </para>
/// </remarks>
public sealed class DdsTexture
{
    private const uint DdpfFourCc = 0x4;
    private const uint DdpfLuminance = 0x20000;

    private DdsTexture(int width, int height, string format, byte[] pixels)
    {
        Width = width;
        Height = height;
        Format = format;
        Pixels = pixels;
    }

    public int Width { get; }
    public int Height { get; }
    public string Format { get; }

    /// <summary>RGBA8, row-major from the top left.</summary>
    public byte[] Pixels { get; }

    public static DdsTexture Parse(ReadOnlySpan<byte> data)
    {
        if (data.Length < 128 || data[0] != (byte)'D' || data[1] != (byte)'D' ||
            data[2] != (byte)'S' || data[3] != (byte)' ')
            throw new DdsException("not a DDS");

        int height = Le.I32(data, 12);
        int width = Le.I32(data, 16);
        uint pfFlags = Le.U32(data, 80);
        string fourCc = System.Text.Encoding.ASCII.GetString(data.Slice(84, 4));

        if (width <= 0 || height <= 0)
            throw new DdsException($"degenerate size {width}x{height}");

        var pixels = new byte[width * height * 4];
        const int body = 128;

        if ((pfFlags & DdpfFourCc) == 0)
            return DecodeUncompressed(data, width, height, pfFlags, pixels, body);

        int blockSize = fourCc switch
        {
            "DXT1" => 8,
            "DXT3" => 16,
            "DXT5" => 16,
            _ => throw new DdsException($"unsupported compression '{fourCc}'"),
        };

        int blocksX = (width + 3) / 4;
        int blocksY = (height + 3) / 4;
        if (body + (long)blocksX * blocksY * blockSize > data.Length)
            throw new DdsException($"{fourCc} payload truncated");

        for (int by = 0; by < blocksY; by++)
        for (int bx = 0; bx < blocksX; bx++)
        {
            int at = body + (by * blocksX + bx) * blockSize;
            switch (fourCc)
            {
                case "DXT1":
                    ColourBlock(data, at, pixels, bx * 4, by * 4, width, height, false);
                    break;
                case "DXT3":
                    ColourBlock(data, at + 8, pixels, bx * 4, by * 4, width, height, true);
                    Dxt3Alpha(data, at, pixels, bx * 4, by * 4, width, height);
                    break;
                default:
                    ColourBlock(data, at + 8, pixels, bx * 4, by * 4, width, height, true);
                    Dxt5Alpha(data, at, pixels, bx * 4, by * 4, width, height);
                    break;
            }
        }
        return new DdsTexture(width, height, fourCc, pixels);
    }

    /// <summary>
    /// Uncompressed formats, driven by the channel masks.
    /// </summary>
    /// <remarks>
    /// Mask-driven rather than special-cased by depth, because the build ships
    /// B8G8R8A8, L8 luminance <i>and</i> X1R5G5B5, and a depth switch would miss
    /// the last two.
    /// </remarks>
    private static DdsTexture DecodeUncompressed(
        ReadOnlySpan<byte> data, int width, int height, uint pfFlags,
        byte[] pixels, int body)
    {
        int bitCount = Le.I32(data, 88);
        uint rMask = Le.U32(data, 92);
        uint gMask = Le.U32(data, 96);
        uint bMask = Le.U32(data, 100);
        uint aMask = Le.U32(data, 104);

        if (bitCount is not (8 or 16 or 24 or 32))
            throw new DdsException($"unsupported uncompressed depth {bitCount}");

        int stride = bitCount / 8;
        if (body + (long)width * height * stride > data.Length)
            throw new DdsException("uncompressed payload truncated");

        bool luminance = (pfFlags & DdpfLuminance) != 0;
        for (int i = 0; i < width * height; i++)
        {
            int at = body + i * stride;
            uint value = 0;
            for (int b = 0; b < stride; b++)
                value |= (uint)data[at + b] << (8 * b);

            byte r, g, bl;
            if (luminance)
                r = g = bl = Channel(value, rMask);
            else
            {
                r = Channel(value, rMask);
                g = Channel(value, gMask);
                bl = Channel(value, bMask);
            }

            pixels[i * 4 + 0] = r;
            pixels[i * 4 + 1] = g;
            pixels[i * 4 + 2] = bl;
            pixels[i * 4 + 3] = aMask != 0 ? Channel(value, aMask) : (byte)255;
        }
        return new DdsTexture(width, height, $"RAW{bitCount}", pixels);
    }

    private static byte Channel(uint value, uint mask)
    {
        if (mask == 0) return 0;
        int shift = System.Numerics.BitOperations.TrailingZeroCount(mask);
        uint span = mask >> shift;
        if (span == 0) return 0;
        return (byte)(((value & mask) >> shift) * 255 / span);
    }

    private static void ColourBlock(ReadOnlySpan<byte> data, int at, byte[] outPixels,
                                    int ox, int oy, int width, int height, bool opaque)
    {
        ushort c0 = BinaryPrimitives.ReadUInt16LittleEndian(data[at..]);
        ushort c1 = BinaryPrimitives.ReadUInt16LittleEndian(data[(at + 2)..]);
        uint bits = Le.U32(data, at + 4);

        Span<byte> palette = stackalloc byte[16];
        Rgb565(c0, palette, 0);
        Rgb565(c1, palette, 4);
        palette[3] = palette[7] = 255;

        if (c0 > c1 || opaque)
        {
            for (int i = 0; i < 3; i++)
            {
                palette[8 + i] = (byte)((2 * palette[i] + palette[4 + i]) / 3);
                palette[12 + i] = (byte)((palette[i] + 2 * palette[4 + i]) / 3);
            }
            palette[11] = palette[15] = 255;
        }
        else
        {
            for (int i = 0; i < 3; i++)
            {
                palette[8 + i] = (byte)((palette[i] + palette[4 + i]) / 2);
                palette[12 + i] = 0;
            }
            palette[11] = 255;
            palette[15] = 0;
        }

        for (int py = 0; py < 4; py++)
        {
            int y = oy + py;
            if (y >= height) break;
            for (int px = 0; px < 4; px++)
            {
                int x = ox + px;
                if (x >= width) continue;
                int slot = (int)((bits >> (2 * (4 * py + px))) & 3) * 4;
                int o = (y * width + x) * 4;
                outPixels[o + 0] = palette[slot + 0];
                outPixels[o + 1] = palette[slot + 1];
                outPixels[o + 2] = palette[slot + 2];
                outPixels[o + 3] = palette[slot + 3];
            }
        }
    }

    private static void Rgb565(ushort value, Span<byte> destination, int at)
    {
        int r = (value >> 11) & 0x1F;
        int g = (value >> 5) & 0x3F;
        int b = value & 0x1F;
        // Replicate the high bits into the low ones so 0x1F maps to 255, not 248.
        destination[at + 0] = (byte)((r << 3) | (r >> 2));
        destination[at + 1] = (byte)((g << 2) | (g >> 4));
        destination[at + 2] = (byte)((b << 3) | (b >> 2));
    }

    private static void Dxt3Alpha(ReadOnlySpan<byte> data, int at, byte[] outPixels,
                                  int ox, int oy, int width, int height)
    {
        ulong alpha = BinaryPrimitives.ReadUInt64LittleEndian(data[at..]);
        for (int py = 0; py < 4; py++)
        {
            int y = oy + py;
            if (y >= height) break;
            for (int px = 0; px < 4; px++)
            {
                int x = ox + px;
                if (x >= width) continue;
                int nibble = (int)((alpha >> (4 * (4 * py + px))) & 0xF);
                outPixels[(y * width + x) * 4 + 3] = (byte)(nibble * 17);
            }
        }
    }

    private static void Dxt5Alpha(ReadOnlySpan<byte> data, int at, byte[] outPixels,
                                  int ox, int oy, int width, int height)
    {
        byte a0 = data[at], a1 = data[at + 1];
        ulong bits = 0;
        for (int i = 0; i < 6; i++)
            bits |= (ulong)data[at + 2 + i] << (8 * i);

        Span<byte> table = stackalloc byte[8];
        table[0] = a0;
        table[1] = a1;
        if (a0 > a1)
            for (int i = 1; i < 7; i++)
                table[i + 1] = (byte)(((7 - i) * a0 + i * a1) / 7);
        else
        {
            for (int i = 1; i < 5; i++)
                table[i + 1] = (byte)(((5 - i) * a0 + i * a1) / 5);
            table[6] = 0;
            table[7] = 255;
        }

        for (int py = 0; py < 4; py++)
        {
            int y = oy + py;
            if (y >= height) break;
            for (int px = 0; px < 4; px++)
            {
                int x = ox + px;
                if (x >= width) continue;
                int index = (int)((bits >> (3 * (4 * py + px))) & 7);
                outPixels[(y * width + x) * 4 + 3] = table[index];
            }
        }
    }
}

public sealed class DdsException(string message) : Exception(message);
