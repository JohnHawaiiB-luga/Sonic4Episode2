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

## What is not done, and why

**There is no Android head project yet**, because it cannot be built or tested on
this machine.

`dotnet workload install android` succeeded — the workload is present at version
`35.0.105/9.0.100`. But building for Android also needs the **Android SDK** and a
**JDK**, which the workload does not bring:

```
error XA5300: The Android SDK directory could not be found.
```

The supported fix is:

```
dotnet build -t:InstallAndroidDependencies -f net8.0-android \
    -p:AndroidSdkDirectory=<path> -p:JavaSdkDirectory=<path> \
    -p:AcceptAndroidSDKLicenses=True
```

That last flag accepts Google's Android SDK licence terms. **That is a legal
acceptance and belongs to whoever owns the machine**, not to a build script
running unattended, so it has been left alone.

Writing an Android head without being able to compile it would mean shipping a few
hundred lines of unverifiable code and calling the phase progressed. The parts
that *can* be verified without a device — content access, input mapping, the
renderer's platform-neutrality — are done and tested instead.

## What remains once the SDK is installed

1. A `Sonic4Episode2.Android` project targeting `net8.0-android`, referencing
   `MonoGame.Framework.Android` instead of `.DesktopGL`.
2. `AndroidContent : IContentSource` over `AssetManager` — the game data goes in
   `Assets/`, and `AssetManager.Open` gives a stream per path. Note that
   `AssetManager` cannot list recursively the way `Directory` can, so `List` needs
   a manifest generated at packaging time.
3. `TouchInput : IInputSource` feeding `VirtualPad` from
   `TouchPanel.GetState()`.
4. A `MainActivity` hosting `StageViewerGame`.

The game data is several gigabytes, which is far past what an APK can carry, so
it has to be sideloaded to external storage and the content source pointed at it.
That is a packaging decision the director should make before the head is written.

## iOS

Same shape, and the same blocker plus one more: iOS builds need a Mac. Nothing
here prevents it, and the abstractions above are the work that would otherwise
have to be redone per platform.
