# aether-mobile

A touch client for iPad (iPadOS) that links `libaethercore` in-process and
talks to the radio directly, with no PC, `aetherd` or SmartSDR in between. It is
configured only when `AETHER_MOBILE` is ON (see `BUILD-OPTIONS.md`).

Status: prototype. The client does the following:

- Discovers LAN radios and lists the built-in demo radio. It can also connect
  by IP address and remembers the last address.
- Connects to a radio. The engine creates the panadapter, the slice and the
  RX audio stream on connect.
- Plays RX audio through the default output.
- Shows the active panadapter and waterfall. Tap the display to tune, drag it
  horizontally, or tap the frequency for direct entry.
- Offers band buttons that use the radio's band stack, which restores each
  band's last frequency and mode (Flex).
- Controls mode, filter width presets, AF gain and mute.
- Keeps the screen awake while connected (iOS).
- Transmits with hold-to-talk PTT from this device's microphone. PTT is
  offered only when the radio's mic input is `PC`; "Use this device's
  microphone" switches it on the radio. Keying goes through the engine's
  `TxController` input. Every TX audio packet passes the `TxCoordinator`
  dispatch fence. Release, disconnect and loss of foreground all unkey.

## Develop on Linux or macOS

```bash
scripts/setup/setup-qt.sh            # pinned Qt, includes Qt Quick
cmake -B build-mobile -DAETHER_MOBILE=ON
cmake --build build-mobile --target aether-mobile --parallel
build-mobile/mobile/aether-mobile --demo
```

The `--demo` option connects to the simulator as soon as it is listed. Add
`--screenshot out.png` to save the window after a delay and quit, which also
works with `QT_QPA_PLATFORM=offscreen`.

## iOS

`.github/workflows/mobile-ios.yml` builds the engine and the app for iOS arm64
(iPadOS 18+) on a GitHub-hosted macOS runner. It uploads
`aether-mobile-unsigned.ipa` as a run artifact. No Mac is needed.

The .ipa is unsigned. Install it in one of two ways:

- **Free Apple ID, sideloaded from a computer** (Linux: Sideloader or
  AltServer-Linux; Windows/macOS: AltStore or Sideloadly). These tools re-sign
  the .ipa with your Apple ID and replace the bundle ID with one unique to
  you. The signature lasts 7 days, and up to 3 apps can be signed this way.
  The iPad must be reachable over USB or Wi-Fi pairing (`usbmuxd`). Enable
  Developer Mode on the iPad (Settings → Privacy & Security) after the first
  install.
- **Apple Developer Program**: the workflow can sign with a distribution
  certificate stored as repository secrets and upload to TestFlight, after
  which the iPad installs from the TestFlight app. With this route you can
  also request the multicast entitlement that LAN discovery needs on iOS.
  Without it, connect by IP address.
