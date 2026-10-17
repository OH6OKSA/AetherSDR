# OksaSDR app icons — hand-off

Rebranded application icons for the OksaSDR fork, plus everything a later
session needs to finish wiring them in. **App-icon / branding only** — no
source, UX, or protocol changes.

> **Name:** keep the app named **OksaSDR** (or another name of your own).
> Do **not** ship it as "AetherSDR" or "AetherSDR Mobile" — an official
> AetherSDR mobile app is on the upstream roadmap, and two near-identical
> names confuse users about which is which and where to get support.
> Crediting the engine in the app description ("Built on the AetherSDR
> engine") is welcome; the name is the only constraint. Upstream is GPLv3,
> so a rebranded fork is fine — keep the license and offer source if you
> distribute a binary.

## The icon

The OksaSDR mark (circular green→blue emblem, circuit-trace / rising-arrow
motif), symbol only, no wordmark, on a white background. Source is a 1024×1024
raster — there is **no vector (SVG)** yet. If this app is ever published for
real, commission an SVG: the fine traces in the mark soften at the smallest
icon sizes, and a vector scales cleanly.

## iOS — DONE (already in the tree)

Path: `mobile/ios/Assets.xcassets/AppIcon.appiconset/`

- `AppIcon-1024.png` replaced with the OksaSDR mark (RGB, **no alpha** — App
  Store requires this; keep it that way).
- Full size set added (`Icon-20.png` … `Icon-180.png`) and `Contents.json`
  expanded to cover iPhone + iPad + ios-marketing.
- The marketing slot still points at `AppIcon-1024.png`, so
  `XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"` in
  `mobile/CMakeLists.txt` keeps working unchanged.

Nothing else to do for iOS. Rebuild and the new icon appears.

## Android — NOT WIRED (files staged here for when Android build exists)

There is currently **no Android build** in this repo: `mobile/CMakeLists.txt`
only has an `if(IOS)` branch — no manifest, no Gradle, no `mipmap` tree. So
the Android icons live here as ready-made assets, not yet referenced by any
build.

When an Android target is added, copy `android/res/` into the Android
project's `app/src/main/res/` (merge with existing `mipmap-*` dirs):

- `mipmap-anydpi-v26/ic_launcher.xml` + `ic_launcher_round.xml` — adaptive icon
- `mipmap-*/ic_launcher_foreground.png` — the mark (transparent)
- `mipmap-*/ic_launcher_background.png` — solid white
- `mipmap-*/ic_launcher.png` / `ic_launcher_round.png` — legacy icons
- densities: mdpi, hdpi, xhdpi, xxhdpi, xxxhdpi

Then in `AndroidManifest.xml`:

```xml
android:icon="@mipmap/ic_launcher"
android:roundIcon="@mipmap/ic_launcher_round"
```

The mark is sized to the 108dp adaptive canvas with the symbol kept inside
the ~62dp safe zone, so round / squircle / square masks don't clip it.

`android/play-store/play_store_512.png` — 512×512 Play Console listing icon
(no transparency).

## Source files (`source/`)

- `oksasdr-symbol-1024-white.png` — the mark, white bg, square (icon master)
- `oksasdr-symbol-foreground-xxxhdpi.png` — transparent mark (adaptive fg)
- `oksasdr-logo-full.png` — full logo incl. "Oksa Consulting Oy" wordmark,
  shadow removed (not for app icons; general brand use)
- `icon-preview.png` — the mark previewed at several sizes, iOS-rounded and
  Android-round

## Regenerating

All icons were derived from the 1024 raster: shadow halo flattened to white,
symbol cropped above the wordmark and centered, then resized per platform.
The iOS master is white-background RGB (no alpha); the Android foreground is
the transparent symbol inside the safe zone over a white background layer.
