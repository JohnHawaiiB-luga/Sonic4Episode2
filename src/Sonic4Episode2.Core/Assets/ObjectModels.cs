namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Finds the model archive for a named object.
/// </summary>
/// <remarks>
/// Gimmicks ship as <c>EP2_GMK_&lt;NAME&gt;_MDL.AMB</c>, with textures and motions
/// beside them under the same stem. 65 such archives exist across the build.
/// <para>
/// The catalogue's object names and those archive stems agree often enough to be
/// obviously the same naming, and not often enough to be mechanical:
/// <c>Jetwall04</c> is <c>JETWALL</c> and <c>SandBranch03</c> is
/// <c>SAND_BRANCH</c>, but <c>Avalanche01</c> is <c>AVLNCH</c> and
/// <c>CandleStick</c> is <c>SCONCE</c>. This resolver does only the mechanical
/// part — strip the trailing digits, try the name as-is, as
/// <c>UPPER_SNAKE</c>, and with underscores removed.
/// </para>
/// <para>
/// <b>Nothing here guesses.</b> An abbreviation like <c>AVLNCH</c> is very
/// probably <c>Avalanche</c>, but "very probably" is how wrong data gets into a
/// project that is otherwise careful, so those simply do not resolve until
/// something confirms them.
/// </para>
/// </remarks>
public static class ObjectModels
{
    private const string Prefix = "EP2_GMK_";
    private const string Suffix = "_MDL.AMB";

    /// <summary>
    /// Archive stems an object name could plausibly use, most literal first.
    /// </summary>
    public static IEnumerable<string> CandidateStems(string name)
    {
        string trimmed = name.TrimEnd('0', '1', '2', '3', '4', '5', '6', '7', '8', '9');
        if (trimmed.Length == 0) trimmed = name;

        yield return trimmed.ToUpperInvariant();
        yield return Snake(trimmed);
        yield return trimmed.Replace("_", "").ToUpperInvariant();
        yield return Snake(trimmed).Replace("_", "");
        yield return name.ToUpperInvariant();
    }

    /// <summary>
    /// Whether an archive stem is an abbreviation of an object name.
    /// </summary>
    /// <remarks>
    /// True when the stem's letters appear in the name in order —
    /// <c>AVLNCH</c> inside <c>AVALANCHE</c>, <c>SANDTANK</c> inside
    /// <c>SANDTRANK</c>. It is deliberately strict about order, which is what
    /// makes it reject the renames: <c>SCONCE</c> is not a subsequence of
    /// <c>CANDLESTICK</c>, and a rule that accepted it would accept anything.
    /// <para>
    /// On its own this is suggestive, not proof. It is only used together with
    /// the zone check — see <see cref="Resolve"/>.
    /// </para>
    /// </remarks>
    public static bool IsAbbreviationOf(string stem, string name)
    {
        string s = Letters(stem), n = Letters(name);
        if (s.Length == 0 || s.Length > n.Length) return false;

        // An abbreviation drops letters; it does not drop half the word. Without
        // this, WATER matches WaterSlider — and WATER is the water surface, a
        // different object entirely.
        if (s.Length * 100 < n.Length * MinimumCoveragePercent) return false;

        int at = 0;
        foreach (char c in n)
            if (at < s.Length && s[at] == c) at++;
        return at == s.Length;
    }

    /// <summary>
    /// How much of a name an abbreviation has to keep to count as one.
    /// </summary>
    /// <remarks>
    /// 60% keeps <c>AVLNCH</c> for <c>Avalanche</c> (67%) and <c>SAND_TANK</c>
    /// for <c>SandTrank</c> (89%), and rejects <c>WATER</c> for
    /// <c>WaterSlider</c> (45%), which is a different object.
    /// </remarks>
    public const int MinimumCoveragePercent = 60;

    /// <summary>
    /// Picks the archive for an object out of the ones actually available.
    /// </summary>
    /// <remarks>
    /// Exact stems win outright. Otherwise an abbreviation is accepted **only if
    /// exactly one candidate is an abbreviation of the name** — two would mean
    /// the evidence does not distinguish them, and the right answer then is to
    /// resolve nothing rather than to guess between them.
    /// </remarks>
    public static string? Resolve(string name, IEnumerable<string> archivePaths)
    {
        var stems = CandidateStems(name).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var paths = archivePaths.ToArray();

        foreach (string path in paths)
            if (stems.Contains(StemOf(path)))
                return path;

        var abbreviated = paths.Where(p => IsAbbreviationOf(StemOf(p), name)).ToArray();
        return abbreviated.Length == 1 ? abbreviated[0] : null;
    }

    /// <summary>The <c>NAME</c> out of <c>.../EP2_GMK_NAME_MDL.AMB</c>.</summary>
    public static string StemOf(string archivePath)
    {
        string file = archivePath[(archivePath.LastIndexOf('/') + 1)..];
        if (!file.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase) ||
            !file.EndsWith(Suffix, StringComparison.OrdinalIgnoreCase))
            return "";
        return file[Prefix.Length..^Suffix.Length].ToUpperInvariant();
    }

    private static string Letters(string value)
    {
        var sb = new System.Text.StringBuilder(value.Length);
        foreach (char c in value)
            if (char.IsLetter(c)) sb.Append(char.ToUpperInvariant(c));
        return sb.ToString();
    }

    /// <summary>
    /// The model archive for an object, or null when nothing matches.
    /// </summary>
    /// <param name="name">A catalogue name, e.g. <c>Jetwall04</c>.</param>
    /// <param name="content">Where to look.</param>
    /// <param name="searchRoots">Directories to search, e.g. the zone's and G_COM's.</param>
    public static string? Find(string name, IContentSource content,
                               IEnumerable<string> searchRoots)
    {
        var stems = CandidateStems(name).Distinct().ToArray();
        foreach (string root in searchRoots)
        {
            foreach (string stem in stems)
            {
                string path = root.Length == 0
                    ? $"{Prefix}{stem}{Suffix}"
                    : $"{root}/{Prefix}{stem}{Suffix}";
                if (content.Exists(path)) return path;
            }
        }
        return null;
    }

    /// <summary>The texture archive beside a model archive.</summary>
    public static string TexturesFor(string modelPath) =>
        modelPath[..^Suffix.Length] + "_TEX.AMB";

    /// <summary>
    /// Turns <c>DashPanel</c> into <c>DASH_PANEL</c>, which is how several of
    /// these archives are spelt.
    /// </summary>
    private static string Snake(string name)
    {
        var sb = new System.Text.StringBuilder(name.Length + 4);
        for (int i = 0; i < name.Length; i++)
        {
            if (i > 0 && char.IsUpper(name[i]) && !char.IsUpper(name[i - 1]) && name[i - 1] != '_')
                sb.Append('_');
            sb.Append(char.ToUpperInvariant(name[i]));
        }
        return sb.ToString();
    }
}
