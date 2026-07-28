using Android.App;
using Android.Content.PM;
using Android.OS;
using Android.Views;
using Microsoft.Xna.Framework;
using Sonic4Episode2.Core.Assets;
using Sonic4Episode2.Desktop;

namespace Sonic4Episode2.Android;

/// <summary>
/// The Android head. Mounts a stage and hands it to the shared renderer.
/// </summary>
/// <remarks>
/// Landscape and fullscreen because the game is a side-scroller and the layout
/// assumes thumbs at the bottom corners. The activity itself is thin on purpose:
/// everything it does beyond wiring is in the core library, so the same code path
/// runs under test on a desktop.
/// </remarks>
[Activity(
    Label = "Sonic 4 Episode II",
    MainLauncher = true,
    Icon = "@android:drawable/ic_menu_gallery",
    Theme = "@android:style/Theme.NoTitleBar.Fullscreen",
    ScreenOrientation = ScreenOrientation.SensorLandscape,
    ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize |
                           ConfigChanges.KeyboardHidden)]
public class MainActivity : AndroidGameActivity
{
    /// <summary>
    /// Where a sideloaded copy of the game is expected.
    /// </summary>
    /// <remarks>
    /// The data is several gigabytes, so it cannot live in the APK. Push the
    /// game's folder here with <c>adb push</c> and the app finds it.
    /// </remarks>
    public const string DataFolder = "Sonic4Episode2";

    private Game? _game;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);

        var content = ResolveContent();
        _game = new StageViewerGame(content, "G_ZONE1/MAP/ZONE11_MAP.AMB", new TouchInput());
        SetContentView((View)_game.Services.GetService(typeof(View)));
        _game.Run();
    }

    /// <summary>
    /// Prefers a sideloaded copy on shared storage, falling back to the APK.
    /// </summary>
    private IContentSource ResolveContent()
    {
        foreach (var dir in GetExternalFilesDirs(null) ?? [])
        {
            string? path = dir?.AbsolutePath;
            if (path is null) continue;
            string candidate = System.IO.Path.Combine(path, DataFolder);
            if (Directory.Exists(candidate)) return new AndroidContent(candidate);
        }

        string shared = System.IO.Path.Combine(
            global::Android.OS.Environment.ExternalStorageDirectory?.AbsolutePath ?? "/sdcard",
            DataFolder);
        if (Directory.Exists(shared)) return new AndroidContent(shared);

        return new AndroidContent(Assets!);
    }
}
