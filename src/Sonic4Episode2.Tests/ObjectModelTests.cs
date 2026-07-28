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

public class ObjectAbbreviationTests
{
    [Fact]
    public void AbbreviationsAreSubsequences()
    {
        Assert.True(ObjectModels.IsAbbreviationOf("AVLNCH", "Avalanche01"));
        Assert.True(ObjectModels.IsAbbreviationOf("SAND_TANK", "SandTrank01"));
        Assert.True(ObjectModels.IsAbbreviationOf("JETWALL", "Jetwall04"));
    }

    [Fact]
    public void RenamesAreNot()
    {
        // This is the case the rule exists to reject. SCONCE really is the candle
        // stick's archive, and nothing about the letters says so.
        Assert.False(ObjectModels.IsAbbreviationOf("SCONCE", "CandleStick"));
        Assert.False(ObjectModels.IsAbbreviationOf("NEEDLE", "Spear"));
    }

    [Fact]
    public void OrderMatters()
    {
        Assert.False(ObjectModels.IsAbbreviationOf("HCNLVA", "Avalanche01"));
    }

    [Fact]
    public void ALongerStemThanNameIsNotAnAbbreviation()
    {
        Assert.False(ObjectModels.IsAbbreviationOf("AVALANCHE_LONG", "Avlnch"));
    }

    [Fact]
    public void APrefixThatDropsHalfTheNameIsNotAnAbbreviation()
    {
        // WATER is a real archive and it is the water surface, not the slider.
        Assert.False(ObjectModels.IsAbbreviationOf("WATER", "WaterSlider"));
        Assert.Equal(60, ObjectModels.MinimumCoveragePercent);
    }

    [Fact]
    public void StemsComeOutOfTheArchiveName()
    {
        Assert.Equal("SPRING", ObjectModels.StemOf("G_COM/GMK/EP2_GMK_SPRING_MDL.AMB"));
        Assert.Equal("", ObjectModels.StemOf("G_COM/GMK/SOMETHING_ELSE.AMB"));
    }

    [Fact]
    public void AnExactStemBeatsAnAbbreviation()
    {
        string[] paths =
        [
            "z/EP2_GMK_SPRINGBOARD_MDL.AMB",
            "z/EP2_GMK_SPRING_MDL.AMB",
        ];
        Assert.Equal("z/EP2_GMK_SPRING_MDL.AMB", ObjectModels.Resolve("Spring", paths));
    }

    [Fact]
    public void TwoPlausibleAbbreviationsResolveToNothing()
    {
        // Ambiguity is not a reason to pick one; it is a reason to pick neither.
        // Both keep enough of the name to qualify, so neither wins.
        string[] paths =
        [
            "z/EP2_GMK_AVLNCH_MDL.AMB",
            "z/EP2_GMK_AVALANC_MDL.AMB",
        ];
        Assert.Null(ObjectModels.Resolve("Avalanche01", paths));
    }

    [Fact]
    public void ASingleAbbreviationResolves()
    {
        string[] paths = ["z/EP2_GMK_AVLNCH_MDL.AMB", "z/EP2_GMK_RAIL_MDL.AMB"];
        Assert.Equal("z/EP2_GMK_AVLNCH_MDL.AMB", ObjectModels.Resolve("Avalanche01", paths));
    }
}
