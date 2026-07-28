# Running on a phone

Playing this on a phone is the point of the project, so this is where that stands.

## What is done

**The core library never touches a filesystem.** Everything goes through
`IContentSource` — `Exists`, `Read`, and a `List` that takes a filename suffix
rather than a glob. `FileSystemContent` is the desktop implementation. An Android
head supplies one over `AssetManager`, iOS over its bundle, a browser build over
`fetch`. Verified by mounting four zones through it.

**The renderer is platform-neutral too.** `StageViewerGame` took an installed
game directory and read textures with `Directory.EnumerateFiles`; it now takes an
`IContentSource` and an optional `IInputSource`, so the same class can be the
Android head's game class with no changes.

**Touch input exists and is tested.** `VirtualPad` maps touch points to the same
three inputs a keyboard produces:

```
+---------------------------------------------+
|                                             |
|                  the game                   |
|                                             |
+-------------------+-------------+-----------+
|                   |             |   jump    |
|      steer        |             +-----------+
|                   |             |  crouch   |
+-------------------+-------------+-----------+
```

The layout is fractions of the screen, so it survives any resolution or aspect —
tested at 1920x1080, 2400x1080 and 1080x2400. Jump sits above crouch in the same
zone deliberately: a spin dash is one thumb resting on crouch while the other taps
jump, and the layout has to allow that specific pair.

**Verified end to end.** Zone 1 Act 1 mounts through a content source and Sonic
runs under touch input on a 1080x2400 portrait screen, reaching exactly the speed
the recovered acceleration predicts.

## Android: builds, and produces an APK

**`Sonic4Episode2.Android` exists and builds a signed APK** — 18 MB in Release.
It is a thin activity and two adapters; everything else is the shared code:

| File | What it does |
|------|--------------|
| `MainActivity.cs` | hosts the shared `StageViewerGame`, landscape and fullscreen |
| `AndroidContent.cs` | `IContentSource` over shared storage, falling back to APK assets |
| `TouchInput.cs` | `IInputSource` handing touch points to `VirtualPad` |

The renderer is **not duplicated**. The project links
`../Sonic4Episode2.Desktop/StageViewerGame.cs` directly, which only works because
that class was taken off the filesystem and given pluggable input first.

### Building it

Needs the .NET Android workload plus the Android SDK and a JDK:

```
dotnet workload install android
dotnet build -t:InstallAndroidDependencies -f net8.0-android     -p:AndroidSdkDirectory=C:/Android/sdk -p:JavaSdkDirectory=C:/Android/jdk     -p:AcceptAndroidSDKLicenses=True
dotnet build src/Sonic4Episode2.Android -c Release -t:SignAndroidPackage
```

**One trap worth knowing.** On a machine with a small paging file the JVM cannot
commit its default 532 MB heap and even `java -version` fails, which surfaces as
`error MSB6006: java.exe exited with code 1`. The project sets
`JavaMaximumHeapSize` to 256 MB, and `JAVA_TOOL_OPTIONS=-Xmx256m` fixes the
version probe itself.

### Getting the data onto the device

The game is several gigabytes, so it is **not** packed into the APK. Push a copy
and the app finds it:

```
adb push "Sonic 4 - Episode 2 (Beta 8)" /sdcard/Sonic4Episode2
```

`MainActivity` checks the app's external files directories first, then
`/sdcard/Sonic4Episode2`, then falls back to APK assets for a cut-down build.

### What has not been proven

The APK builds and installs. **It has not been run on a device or emulator**, so
nothing here claims it renders correctly on Android — only that it compiles,
packages, and shares its code path with a renderer that does work on desktop.

## iOS

Same three files with different platform types: an `IContentSource` over the app
bundle, an `IInputSource` over `TouchPanel`, and a `UIApplicationDelegate` hosting
the same `StageViewerGame`.

**It is not written, because iOS builds need a Mac and this is a Windows
machine.** Writing a head that cannot be compiled or run here would be a few
hundred lines taken on faith, and the Android head is the evidence that the
abstractions carry — it needed no changes to the shared renderer at all.

Whoever writes it should expect the same shape as `Sonic4Episode2.Android`, and
the same data problem: several gigabytes will not fit in an app bundle either.
