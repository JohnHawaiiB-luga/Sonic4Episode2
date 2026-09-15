#if PLAYABLE_PREVIEW_GUI
using System.Runtime.InteropServices;
#endif
using System.Text.Json;

namespace Sonic4Episode2.Desktop;

internal sealed record PlayablePreviewDefaults(string GameRoot, string NativeManifest);

internal static class PlayablePreviewBootstrap
{
    private const string ConfigurationFileName = "playable-preview.json";
    private const string ConfigurationFormat = "sonic4episode2-playable-preview-v1";
    private const long MaximumConfigurationBytes = 32 * 1024;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    public static PlayablePreviewDefaults? TryLoad()
    {
        string baseDirectory = AppContext.BaseDirectory;
        string configurationPath = Path.Combine(baseDirectory, ConfigurationFileName);
        if (!File.Exists(configurationPath)) return null;

        try
        {
            using var configurationFile = File.OpenRead(configurationPath);
            if (configurationFile.Length > MaximumConfigurationBytes)
                throw new InvalidDataException($"{ConfigurationFileName} exceeds the size limit");

            var configuration = JsonSerializer.Deserialize<PreviewConfiguration>(configurationFile, JsonOptions)
                ?? throw new InvalidDataException($"{ConfigurationFileName} is empty");
            if (configuration.Format != ConfigurationFormat)
                throw new InvalidDataException($"{ConfigurationFileName} has an unsupported format");

            string gameRoot = ResolvePath(configuration.GameRoot, baseDirectory, "gameRoot");
            if (!Directory.Exists(gameRoot))
                throw new InvalidDataException($"configured gameRoot does not exist: {gameRoot}");

            string nativeManifest = ResolvePath(configuration.NativeManifest, baseDirectory, "nativeManifest");
            if (!File.Exists(nativeManifest))
                throw new InvalidDataException($"configured nativeManifest does not exist: {nativeManifest}");

            string sceneDirectory = Path.GetDirectoryName(nativeManifest)!;
            foreach (string sceneFile in new[] { "ZONE11_A.json", "ZONE11_B.json" })
            {
                string scenePath = Path.Combine(sceneDirectory, sceneFile);
                if (!File.Exists(scenePath))
                    throw new InvalidDataException($"configured native scene file does not exist: {scenePath}");
            }

            return new PlayablePreviewDefaults(gameRoot, nativeManifest);
        }
        catch (InvalidDataException)
        {
            throw;
        }
        catch (Exception error) when (error is JsonException or IOException or UnauthorizedAccessException or
                                      ArgumentException or NotSupportedException)
        {
            throw new InvalidDataException($"cannot read {ConfigurationFileName}: {error.Message}", error);
        }
    }

    public static void ShowStartupError(string message, bool noArgumentLaunch)
    {
#if PLAYABLE_PREVIEW_GUI
        if (!noArgumentLaunch || !OperatingSystem.IsWindows()) return;
        MessageBoxW(0,
            $"The playable preview could not start.\n\n{message}\n\nRun build-playable-preview.ps1 again with the game data root.",
            "Sonic 4 Episode 2 Preview", 0x10);
#endif
    }

    private static string ResolvePath(string? value, string baseDirectory, string propertyName)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new InvalidDataException($"{ConfigurationFileName} requires a non-empty {propertyName}");
        return Path.GetFullPath(value, baseDirectory);
    }

    private sealed class PreviewConfiguration
    {
        public string? Format { get; init; }
        public string? GameRoot { get; init; }
        public string? NativeManifest { get; init; }
    }

#if PLAYABLE_PREVIEW_GUI
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBoxW(nint hWnd, string text, string caption, uint type);
#endif
}
