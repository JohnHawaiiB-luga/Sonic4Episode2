using Sonic4Episode2.Core.Assets;

// Cross-check harness for the C# asset layer.
//
// The Python tools already verify these formats against the whole build. The
// point of this is that the C# port must produce the *same numbers* - if it
// does not, one of the two is wrong, and the discrepancy says where to look.

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: verify <game-root>     archives");
    Console.Error.WriteLine("       models <game-root>     NN containers, geometry, materials");
    Console.Error.WriteLine("       textures <game-root>   DDS decoding");
    Console.Error.WriteLine("       grids  <act-archive.AMB>");
    return 2;
}

string command = args[0];
string target = args[1];

return command switch
{
    "verify" => VerifyArchives(target),
    "models" => VerifyModels(target),
    "textures" => VerifyTextures(target),
    "grids" => ShowGrids(target),
    _ => Fail($"unknown command '{command}'"),
};

static int VerifyTextures(string root)
{
    if (!Directory.Exists(root))
        return Fail($"no such directory: {root}");

    int ok = 0, bad = 0;
    var formats = new Dictionary<string, int>();
    var failures = new List<string>();

    foreach (var path in Directory.EnumerateFiles(root, "*.amb", SearchOption.AllDirectories))
    {
        AmbArchive archive;
        try { archive = AmbArchive.Load(path); }
        catch (Exception ex) when (ex is AmbException or IOException) { continue; }

        foreach (var entry in archive.Entries)
        {
            if (!entry.Name.EndsWith(".DDS", StringComparison.OrdinalIgnoreCase)) continue;
            try
            {
                var texture = DdsTexture.Parse(archive.Read(entry).Span);
                formats[texture.Format] = formats.GetValueOrDefault(texture.Format) + 1;
                ok++;
            }
            catch (Exception ex) when (ex is DdsException or ArgumentOutOfRangeException)
            {
                bad++;
                if (failures.Count < 8)
                    failures.Add($"{Path.GetFileName(path)}::{entry.Name}: {ex.Message}");
            }
        }
    }

    Console.WriteLine($"{ok} textures decoded, {bad} failed");
    foreach (var pair in formats.OrderByDescending(kv => kv.Value))
        Console.WriteLine($"  {pair.Key,-7} {pair.Value}");
    foreach (var failure in failures)
        Console.Error.WriteLine($"  ! {failure}");
    return bad == 0 ? 0 : 1;
}

static int VerifyModels(string root)
{
    if (!Directory.Exists(root))
        return Fail($"no such directory: {root}");

    int containers = 0, containerFails = 0;
    int models = 0, locators = 0, modelFails = 0, skinned = 0;
    int motions = 0, channels = 0;
    long vertices = 0, triangles = 0;
    int textureRefs = 0, textureBound = 0;
    var failures = new List<string>();

    foreach (var path in Directory.EnumerateFiles(root, "*.amb", SearchOption.AllDirectories))
    {
        AmbArchive archive;
        try { archive = AmbArchive.Load(path); }
        catch (Exception ex) when (ex is AmbException or IOException) { continue; }

        foreach (var entry in archive.Entries)
        {
            string ext = Path.GetExtension(entry.Name).ToUpperInvariant();
            if (ext is not (".ZNO" or ".ZNM" or ".ZNV" or ".XNM")) continue;

            var bytes = archive.Read(entry);
            try
            {
                var file = NnFile.Parse(bytes);
                containers++;

                if (ext is ".ZNM" or ".XNM")
                {
                    var motion = file.ReadMotion();
                    if (motion is not null)
                    {
                        var (header, list) = motion.Value;
                        if (header.FrameRate is <= 0 or > 240)
                            throw new NnException($"implausible frame rate {header.FrameRate}");
                        if (header.End < header.Start)
                            throw new NnException("end frame precedes start frame");
                        motions++;
                        channels += list.Count;
                    }
                    continue;
                }
                if (ext != ".ZNO") continue;

                var model = NnModel.Load(bytes);
                if (model is null) continue;
                if (model.Header.IsLocator) { locators++; models++; continue; }

                triangles += model.CountTriangles();
                vertices += model.CountVertices();
                if (model.Header.IsSkinned) skinned++;

                foreach (var mesh in model.MeshSets)
                {
                    int? index = mesh.MaterialIndex >= 0 && mesh.MaterialIndex < model.Materials.Count
                        ? model.Materials[mesh.MaterialIndex].TextureIndex
                        : null;
                    if (index is null) continue;
                    textureRefs++;
                    if (index >= 0 && index < model.TextureNames.Count) textureBound++;
                }
                models++;
            }
            catch (Exception ex) when (ex is NnException or AmbException or ArgumentOutOfRangeException)
            {
                if (ext == ".ZNO") modelFails++; else containerFails++;
                if (failures.Count < 8) failures.Add($"{Path.GetFileName(path)}::{entry.Name}: {ex.Message}");
            }
        }
    }

    Console.WriteLine($"{containers} NN containers parsed, {containerFails} failed");
    Console.WriteLine($"{models} models ({locators} locators, {skinned} skinned), {modelFails} failed");
    Console.WriteLine($"{vertices:N0} vertices and {triangles:N0} triangles extracted");
    Console.WriteLine($"{motions} motions carrying {channels:N0} channels");
    Console.WriteLine($"{textureBound}/{textureRefs} mesh texture bindings resolve");
    foreach (var failure in failures)
        Console.Error.WriteLine($"  ! {failure}");
    return modelFails == 0 && containerFails == 0 ? 0 : 1;
}

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
