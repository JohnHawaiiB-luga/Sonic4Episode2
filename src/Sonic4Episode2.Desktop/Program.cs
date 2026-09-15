using System.Runtime.InteropServices;
using Sonic4Episode2.Desktop;
using Sonic4Episode2.Core;
using Sonic4Episode2.Core.Assets;

// Desktop head. Takes the game root and an act archive, opens a window and
// draws that stage assembled from the original data.
//
// Requires your own copy of the game; nothing here ships assets.

// Render at real pixels. Without this the window is DPI-virtualised on any
// display not at 100% — a 1280x720 backbuffer stretched by the compositor to
// 1600x900 on a 125% screen, which is both blurry and the reason a window
// capture came out 1600x900 with the game letterboxed inside it.
if (OperatingSystem.IsWindows()) Dpi.SetProcessDPIAware();

// --screenshot <path> draws a few frames, writes a PNG and exits, which is how
// the README's pictures of the running renderer are made.
string? screenshot = null;
string? nativeManifest = null;
bool playbackSmoke = false;
bool restartSmoke = false;
bool validateScenes = false;
int? spawn = null;
int? spawnY = null;
var rest = new List<string>();
for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--native-scenes")
    {
        if (nativeManifest is not null || i + 1 >= args.Length || args[i + 1].StartsWith("--"))
        {
            Console.Error.WriteLine("--native-scenes requires one manifest path");
            return 2;
        }
        nativeManifest = args[++i];
    }
    else if (args[i] == "--playback-smoke") playbackSmoke = true;
    else if (args[i] == "--restart-smoke") restartSmoke = true;
    else if (args[i] == "--validate-native-scenes") validateScenes = true;
    else if (args[i] == "--screenshot" && i + 1 < args.Length) screenshot = args[++i];
    else if (args[i] == "--spawn" && i + 1 < args.Length)
    {
        // "x" or "x,y" — the row matters because a stage's top rows are often
        // solid backdrop rather than sky.
        var parts = args[++i].Split(',');
        spawn = int.Parse(parts[0]);
        if (parts.Length > 1) spawnY = int.Parse(parts[1]);
    }
    else rest.Add(args[i]);
}

if (playbackSmoke && restartSmoke)
{
    Console.Error.WriteLine("--playback-smoke and --restart-smoke cannot be combined");
    return 2;
}

PlayablePreviewDefaults? previewDefaults = null;
try
{
    previewDefaults = rest.Count == 0 ? PlayablePreviewBootstrap.TryLoad() : null;
}
catch (InvalidDataException error)
{
    Console.Error.WriteLine($"preview: {error.Message}");
    PlayablePreviewBootstrap.ShowStartupError(error.Message, args.Length == 0);
    return 2;
}

string root = rest.Count > 0 ? rest[0] : previewDefaults?.GameRoot ?? ".";
string act = rest.Count > 1 ? rest[1] : "G_ZONE1/MAP/ZONE11_MAP.AMB";
nativeManifest ??= previewDefaults?.NativeManifest;

if (!Directory.Exists(root))
{
    string message = $"game root not found: {root}";
    Console.Error.WriteLine(message);
    PlayablePreviewBootstrap.ShowStartupError(message, args.Length == 0);
    return 2;
}

try
{
    IContentSource content = new FileSystemContent(root);
    var scenes = nativeManifest is null ? null : NativeStagePreview.Load(nativeManifest, root, act, out content);
    if (validateScenes)
    {
        if (scenes is null) throw new InvalidDataException("--validate-native-scenes requires --native-scenes");
        Console.WriteLine($"native preview validated: {scenes.Values.Sum(scene => scene.Instances.Count)} main-layer instances");
        return 0;
    }
    if (scenes is not null)
        Console.WriteLine($"native preview: {string.Join(", ", scenes.Keys)}; other layers use prototype placement");
    using var game = new StageViewerGame(content, act, null)
    {
        ScreenshotPath = screenshot,
        SpawnCellX = spawn,
        SpawnCellY = spawnY,
        NativeScenes = scenes,
        PlaybackSmoke = playbackSmoke || restartSmoke,
        RestartSmoke = restartSmoke,
    };
    game.Run();
    return playbackSmoke || restartSmoke ? game.PlaybackSmokeExitCode : 0;
}
catch (Exception error) when (error is InvalidDataException or IOException or UnauthorizedAccessException)
{
    string message = $"preview: {error.Message}";
    Console.Error.WriteLine(message);
    PlayablePreviewBootstrap.ShowStartupError(message, args.Length == 0);
    return 2;
}

static class Dpi
{
    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();
}
