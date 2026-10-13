# aether-mobile

A touch client for iPad (iPadOS) that links `libaethercore` in-process and
talks to the radio directly, with no PC, `aetherd` or SmartSDR in between. It is
configured only when `AETHER_MOBILE` is ON (see `BUILD-OPTIONS.md`).

Privacy policy: [PRIVACY.md](PRIVACY.md).

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

## README screenshots

`mobile/tools/update-screenshots.sh [RADIO_IP]` builds the app and renders
`docs/mobile/*.jpg` at every layout size (iPad Pro, iPad mini portrait, and
iPhone portrait and landscape), plus the LSB passband from the demo radio.
Run it with every user-interface change and commit the images with it.

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

### Turning on TestFlight (once the Apple Developer account is active)

1. **Bundle ID.** In developer.apple.com → Certificates, IDs & Profiles →
   Identifiers, register an App ID such as `fi.example.aethersdr`.
2. **App record.** In App Store Connect → Apps → "+" → New App, choose
   iOS, the bundle ID from step 1 and any name and SKU.
3. **API key.** In App Store Connect → Users and Access → Integrations →
   App Store Connect API, create a key with the *App Manager* role. Note
   the Issuer ID and Key ID, and download the `.p8` file. Apple allows the
   download only once.
4. **Team ID.** Copy it from developer.apple.com → Membership details.
5. **GitHub.** In the fork, open Settings → Secrets and variables → Actions:
   - Secrets: `APPLE_TEAM_ID`, `APPSTORE_API_KEY_ID`,
     `APPSTORE_API_ISSUER_ID`, and `APPSTORE_API_KEY_P8` (paste the whole
     `.p8` file, including the BEGIN/END lines).
   - Variable: `AETHER_MOBILE_BUNDLE_ID` set to the bundle ID from step 1.

The next push to a `mobile/**` branch, or a manual run of the workflow,
archives, signs and uploads the build. After Apple finishes processing it
(usually 5–30 minutes), add testers in App Store Connect → TestFlight.
Testers install the TestFlight app and accept the invitation. Each tester
enters their own radio's IP address in the app.



## Developer link

For on-device debugging the app can stream its log to a developer PC and
take screenshots on request. It is off by default; turn it on in the app
(Log page → Developer link, enter the PC's address). On the PC run

    mobile/tools/devlink.py serve --out /some/dir
    mobile/tools/devlink.py send '{"cmd":"screenshot"}'

The app connects out to port 47100 (allow it in the PC's firewall). It
accepts only receive and display commands (screenshot, state, set on an
allow-list, tune, mode, band, zoom, drawer); nothing on the link can key
the transmitter.
