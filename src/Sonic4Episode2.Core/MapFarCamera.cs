using System.Numerics;
using System.Security.Cryptography;

namespace Sonic4Episode2.Core;

public static class MapFarCamera
{
    public const string FirstActArchive = "G_ZONE1/MAPFAR/EP2_MAPFAR_ZONE1.AMB";
    public const string SettingsArchive = "G_COM/SETTING/GM_SETTING_MAPFAR.AMB";
    public const float FirstActFieldOfView = (float)(0x0E0F * 9.58738019107841E-05);
    public const float NearPlane = 0.1f;
    public const float FarPlane = 32768f;

    public static void ValidateFirstAct(ReadOnlySpan<byte> background, ReadOnlySpan<byte> settings)
    {
        if (Convert.ToHexString(SHA256.HashData(background)) !=
                "C7722213C35303DDF93D6A25005719DBA013F12278860331CDDF58999C07FB4D" ||
            Convert.ToHexString(SHA256.HashData(settings)) !=
                "A7309F9EC6C42C3BC72CB2FC8E55ABD22B46CF5C98B790FBFBEF1A1C6F3EEE21")
            throw new InvalidDataException("unsupported first-act background or camera settings fingerprint");
    }

    public static Vector3 FirstActPosition(Vector2 mainCamera)
    {
        if (!float.IsFinite(mainCamera.X) || !float.IsFinite(mainCamera.Y) ||
            MathF.Abs(mainCamera.X) > 16777216f || MathF.Abs(mainCamera.Y) > 16777216f)
            throw new ArgumentOutOfRangeException(nameof(mainCamera));
        float x = mainCamera.X * (200f / (510 * 64));
        float y = (mainCamera.Y + 70 * 64) * (20f / (70 * 64));
        return new Vector3(Scroll(x, -200, 200), Scroll(y, 0, 20), 160);
    }

    public static Vector3 FirstActFollowOffset(Vector3 cameraPosition)
        => new(cameraPosition.X + 200, cameraPosition.Y, 0);

    private static float Scroll(float position, int start, int length)
    {
        if (position >= length) return start + length;
        float remainder = position - (int)position / length * length;
        return remainder + start;
    }
}
