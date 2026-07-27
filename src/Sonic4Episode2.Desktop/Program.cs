using Sonic4Episode2.Desktop;

// Desktop head. Takes the game root and an act archive, opens a window and
// draws that stage assembled from the original data.
//
// Requires your own copy of the game; nothing here ships assets.

string root = args.Length > 0 ? args[0] : ".";
string act = args.Length > 1 ? args[1] : "G_ZONE1/MAP/ZONE11_MAP.AMB";

if (!Directory.Exists(root))
{
    Console.Error.WriteLine($"game root not found: {root}");
    return 2;
}

using var game = new StageViewerGame(root, act);
game.Run();
return 0;
