using Sonic4Episode2.Core.Assets;

namespace Sonic4Episode2.Tests;

public class ObjectModelTests
{
    private const string Root =
        @"C:\Users\DavidErikGarciaArena\Downloads\Sonic 4 - Episode 2 (Beta 8)\Sonic 4 - Episode 2 (Beta 8)";

    private static readonly string[] Roots =
        ["G_COM/GMK", "G_ZONE1/GMK", "G_ZONE2/GMK", "G_ZONE3/GMK", "G_ZONE4/GMK"];

    [Fact]
    public void TrailingDigitsAreStripped()
    {
        Assert.Contains("JETWALL", ObjectModels.CandidateStems("Jetwall04"));
        Assert.Contains("SPRING", ObjectModels.CandidateStems("Spring"));
    }

    [Fact]
    public void CamelCaseBecomesUnderscored()
    {
        Assert.Contains("SAND_BRANCH", ObjectModels.CandidateStems("SandBranch03"));
        Assert.Contains("METAL_UNIT", ObjectModels.CandidateStems("MetalUnit03"));
    }

    [Fact]
    public void AnAllDigitsNameDoesNotVanish()
    {
        Assert.NotEmpty(ObjectModels.CandidateStems("01"));
    }

    [Fact]
    public void TexturesSitBesideTheModel()
    {
        Assert.Equal("G_COM/GMK/EP2_GMK_SPRING_TEX.AMB",
                     ObjectModels.TexturesFor("G_COM/GMK/EP2_GMK_SPRING_MDL.AMB"));
    }

    [Fact]
    public void KnownObjectsResolveAgainstTheInstalledGame()
    {
        if (!Directory.Exists(Root)) return;
        var content = new FileSystemContent(Root);

        // Each of these was confirmed by hand against the shipped archives.
        Assert.NotNull(ObjectModels.Find("Spring", content, Roots));
        Assert.NotNull(ObjectModels.Find("Jetwall04", content, Roots));
        Assert.NotNull(ObjectModels.Find("SandBranch03", content, Roots));
        Assert.NotNull(ObjectModels.Find("Propeller01", content, Roots));
    }

    [Fact]
    public void AnAbbreviatedNameDoesNotResolve()
    {
        if (!Directory.Exists(Root)) return;
        var content = new FileSystemContent(Root);

        // The archive is EP2_GMK_AVLNCH_MDL.AMB. That is almost certainly this
        // object, and "almost certainly" is deliberately not good enough here.
        Assert.Null(ObjectModels.Find("Avalanche01", content, Roots));
    }

    [Fact]
    public void AnInventedNameResolvesToNothing()
    {
        if (!Directory.Exists(Root)) return;
        var content = new FileSystemContent(Root);
        Assert.Null(ObjectModels.Find("NotAnObject", content, Roots));
    }
}
