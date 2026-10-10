# aether-mobile

A touch client for iPad (iPadOS) that links `libaethercore` in-process and
talks to the radio directly, with no PC, `aetherd` or SmartSDR in between. It is
configured only when `AETHER_MOBILE` is ON (see `BUILD-OPTIONS.md`).

Status: prototype. The client does the following:

- Discovers LAN radios and lists the built-in demo radio.
- Connects to a radio. The engine creates the panadapter, the slice and the
  RX audio stream on connect.
- Plays RX audio through the default output.
- Shows the active panadapter and waterfall. Tap the display to tune, or
  drag it horizontally.
- Controls frequency, mode, AF gain and mute for the active slice.

Transmit is deliberately unavailable. The PTT control stays disabled and shows
the reason until the microphone path, the `TxController` input and the
`TxCoordinator` dispatch fence are wired (Constitution Principle VI).

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

`.github/workflows/mobile-ios.yml` cross-compiles the engine for iOS arm64 on a
GitHub-hosted macOS runner. Building, signing and installing the app bundle on a
device is the next step.
