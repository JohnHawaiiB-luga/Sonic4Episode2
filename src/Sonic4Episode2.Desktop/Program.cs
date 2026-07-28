using Sonic4Episode2.Desktop;

// Desktop head. Takes the game root and an act archive, opens a window and
// draws that stage assembled from the original data.
//
// Requires your own copy of the game; nothing here ships assets.

// --screenshot <path> draws a few frames, writes a PNG and exits, which is how
// the README's pictures of the running renderer are made.
string? screenshot = null;
int? spawn = null;
int? spawnY = null;
var rest = new List<string>();
for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--screenshot" && i + 1 < args.Length) screenshot = args[++i];
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

string root = rest.Count > 0 ? rest[0] : ".";
string act = rest.Count > 1 ? rest[1] : "G_ZONE1/MAP/ZONE11_MAP.AMB";

if (!Directory.Exists(root))
{
    Console.Error.WriteLine($"game root not found: {root}");
    return 2;
}

using var game = new StageViewerGame(root, act)
{
    ScreenshotPath = screenshot,
    SpawnCellX = spawn,
    SpawnCellY = spawnY,
};
game.Run();
return 0;
