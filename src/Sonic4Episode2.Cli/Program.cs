using Sonic4Episode2.Core.Assets;

// Cross-check harness for the C# asset layer.
//
// The Python tools already verify these formats against the whole build. The
// point of this is that the C# port must produce the *same numbers* - if it
// does not, one of the two is wrong, and the discrepancy says where to look.

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: verify <game-root>");
    Console.Error.WriteLine("       grids  <act-archive.AMB>");
    return 2;
}

string command = args[0];
string target = args[1];

return command switch
{
    "verify" => VerifyArchives(target),
    "grids" => ShowGrids(target),
    _ => Fail($"unknown command '{command}'"),
};

static int Fail(string message)
{
    Console.Error.WriteLine(message);
    return 2;
}

static int VerifyArchives(string root)
{
    if (!Directory.Exists(root))
        return Fail($"no such directory: {root}");

    int ok = 0, bad = 0;
    var extensions = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);

    foreach (var path in Directory.EnumerateFiles(root, "*.amb", SearchOption.AllDirectories))
    {
        try
        {
            var archive = AmbArchive.Load(path);
            var problems = archive.Validate();
            if (problems.Count > 0)
            {
                bad++;
                Console.Error.WriteLine($"FAIL {Path.GetFileName(path)}: {problems[0]}");
                continue;
            }
            ok++;
            foreach (var entry in archive.Entries)
            {
                string ext = Path.GetExtension(entry.Name);
                if (ext.Length == 0) ext = "(none)";
                extensions[ext] = extensions.GetValueOrDefault(ext) + 1;
            }
        }
        catch (Exception ex) when (ex is AmbException or IOException)
        {
            bad++;
            Console.Error.WriteLine($"FAIL {Path.GetFileName(path)}: {ex.Message}");
        }
    }

    Console.WriteLine($"{ok} archives parsed cleanly, {bad} failed");
    Console.WriteLine();
    Console.WriteLine("contained file types:");
    foreach (var pair in extensions.OrderByDescending(kv => kv.Value).Take(12))
        Console.WriteLine($"  {pair.Key,10}  {pair.Value}");
    return bad == 0 ? 0 : 1;
}

static int ShowGrids(string archivePath)
{
    if (!File.Exists(archivePath))
        return Fail($"no such file: {archivePath}");

    var archive = AmbArchive.Load(archivePath);
    int shown = 0;
    foreach (var entry in archive.Entries)
    {
        string ext = Path.GetExtension(entry.Name);
        if (!ext.Equals(".MP", StringComparison.OrdinalIgnoreCase) &&
            !ext.Equals(".MD", StringComparison.OrdinalIgnoreCase))
            continue;

        // Names carry a leading ".\.\" path prefix in these archives.
        string label = entry.Name.Replace('\\', '/');
        label = label[(label.LastIndexOf('/') + 1)..];

        var grid = StageGrid.Parse(label, archive.Read(entry).Span);
        Console.WriteLine(
            $"  {label,-22} {grid.Width,4}x{grid.Height,-4} u{grid.Depth * 8,-2} " +
            $"occupancy {grid.Occupancy,7:P1}");
        shown++;
    }
    Console.WriteLine($"{shown} grids");
    return shown > 0 ? 0 : 1;
}
