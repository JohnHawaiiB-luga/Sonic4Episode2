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
