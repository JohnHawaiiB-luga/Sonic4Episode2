using System.Buffers.Binary;

namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// The decoded keys of one animation channel, and how to sample them.
/// </summary>
/// <remarks>
/// <see cref="NnSubMotion"/> parses a channel's header — which node it drives,
/// how many keys, their size and where they are. This decodes the keys those
/// fields point at and evaluates the curve at any frame, which is what actually
/// animates a model.
/// <para>
/// The keys come in two encodings, told apart by size and confirmed across
/// 276,662 channels in the build:
/// <list type="bullet">
/// <item><b>8 bytes</b> — a float frame and a float value. Translation and scale;
///   79,570 of 79,570 have monotonic frames.</item>
/// <item><b>4 bytes</b> — a <b>signed</b> 16-bit frame packed with a signed 16-bit
///   A16 value, 65536 to a turn. Rotation. Monotonic across 197,092 channels once
///   the frame is read as signed — the negative starts are blend pre-roll, the
///   same negative frames <see cref="NnMotion"/> allows.</item>
/// </list>
/// The rotation value is A16, the third place this format keeps an angle as an
/// integer where a float would be assumed. See <see cref="NnNode"/>.
/// </para>
/// </remarks>
public sealed class MotionSampler
{
    /// <summary>Low-byte tag of a packed A16 rotation channel.</summary>
    public const int RotationEncoding = 0x12;

    private readonly float[] _frames;
    private readonly float[] _values;

    private MotionSampler(int target, bool isRotation, float[] frames, float[] values)
    {
        Target = target;
        IsRotation = isRotation;
        _frames = frames;
        _values = values;
    }

    /// <summary>The node this channel drives.</summary>
    public int Target { get; }

    /// <summary>Whether the values are angles, returned from <see cref="Sample"/> in radians.</summary>
    public bool IsRotation { get; }

    public int KeyCount => _frames.Length;
    public float FrameOf(int key) => _frames[key];

    /// <summary>
    /// Decodes a channel's keys, or null when its header does not point at valid
    /// key data.
    /// </summary>
    public static MotionSampler? Decode(NnSubMotion channel, ReadOnlySpan<byte> data)
    {
        if (channel.KeyCount < 0 || channel.KeySize is not (4 or 8)) return null;

        int at = NnFile.DataBase + channel.KeyOffset;
        if (at < NnFile.DataBase ||
            at + (long)channel.KeyCount * channel.KeySize > data.Length)
            return null;

        var frames = new float[channel.KeyCount];
        var values = new float[channel.KeyCount];
        bool rotation = (channel.Flags & 0xFF) == RotationEncoding;

        for (int i = 0; i < channel.KeyCount; i++)
        {
            int k = at + i * channel.KeySize;
            if (channel.KeySize == 8)
            {
                frames[i] = BitConverter.ToSingle(data[k..]);
                values[i] = BitConverter.ToSingle(data[(k + 4)..]);
            }
            else
            {
                frames[i] = BinaryPrimitives.ReadInt16LittleEndian(data[k..]);
                values[i] = BinaryPrimitives.ReadInt16LittleEndian(data[(k + 2)..]);
            }
        }
        return new MotionSampler(channel.Target, rotation, frames, values);
    }

    /// <summary>
    /// The value at a frame, linearly interpolated, in radians for a rotation.
    /// </summary>
    /// <remarks>
    /// Outside the key range the value holds at the nearest key rather than
    /// extrapolating, which is what a clamped animation does at its ends.
    /// </remarks>
    public float Sample(float frame)
    {
        if (_frames.Length == 0) return 0f;
        if (frame <= _frames[0]) return Convert(_values[0]);
        if (frame >= _frames[^1]) return Convert(_values[^1]);

        int hi = 1;
        while (hi < _frames.Length && _frames[hi] < frame) hi++;
        int lo = hi - 1;

        float span = _frames[hi] - _frames[lo];
        float t = span > 0f ? (frame - _frames[lo]) / span : 0f;
        return Convert(_values[lo] + (_values[hi] - _values[lo]) * t);
    }

    private float Convert(float raw) =>
        IsRotation ? raw * MathF.Tau / NnNode.RotationUnitsPerTurn : raw;
}
